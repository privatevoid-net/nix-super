# This is a helper to callFlake() to lazily fetch flake inputs.

# The contents of the lock file, in JSON format.
lockFileStr:

# A mapping of lock file node IDs to { sourceInfo, subdir } attrsets,
# with sourceInfo.outPath providing an InputAccessor to a previously
# fetched tree. This is necessary for possibly unlocked inputs, in
# particular the root input, but also --override-inputs pointing to
# unlocked trees.
overrides:

let

  lockFile = builtins.fromJSON lockFileStr;

  # Resolve a input spec into a node name. An input spec is
  # either a node name, or a 'follows' path from the root
  # node.
  resolveInput = inputSpec:
    if builtins.isList inputSpec
    then getInputByPath lockFile.root inputSpec
    else inputSpec;

  # Follow an input path (e.g. ["dwarffs" "nixpkgs"]) from the
  # root node, returning the final node.
  getInputByPath = nodeName: path:
    if path == []
    then nodeName
    else
      getInputByPath
        # Since this could be a 'follows' input, call resolveInput.
        (resolveInput lockFile.nodes.${nodeName}.inputs.${builtins.head path})
        (builtins.tail path);

  allNodes =
    builtins.mapAttrs
      (key: node:
        let

          sourceInfo =
            if overrides ? ${key}
            then overrides.${key}.sourceInfo
            else if node.locked.type == "path" && builtins.substring 0 1 node.locked.path != "/"
            then
              let
                parentNode = allNodes.${getInputByPath lockFile.root node.parent};
              in parentNode.sourceInfo // {
                outPath = parentNode.sourceInfo.outPath + ("/" + node.locked.path);
              }
            else
              # FIXME: remove obsolete node.info.
              fetchTree (node.info or {} // removeAttrs node.locked ["dir"]);

          # With overrides, the accessor already points to the right subdirectory.
          subdir = if overrides ? ${key} then "" else node.locked.dir or "";

          flake =
            import (sourceInfo.outPath + ((if subdir != "" then "/" else "") + subdir + "/flake.nix"));

          inputs = builtins.mapAttrs
            (inputName: inputSpec: allNodes.${resolveInput inputSpec})
            (node.inputs or {});

          outputs = flake.outputs (inputs // { self = result; });

          result = outputs // sourceInfo // { inherit inputs; inherit outputs; inherit sourceInfo; };
        in
          if node.flake or true then
            assert builtins.isFunction flake.outputs;
            result
          else
            sourceInfo
      )
      lockFile.nodes;

in allNodes.${lockFile.root}
