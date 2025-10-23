# Nix Archive (NAR) format

This is the complete specification of the [Nix Archive] format.
The Nix Archive format closely follows the abstract specification of a [file system object] tree,
because it is designed to serialize exactly that data structure.

[Nix Archive]: @docroot@/store/file-system-object/content-address.md#nix-archive
[file system object]: @docroot@/store/file-system-object.md

The format of this specification is close to [Extended Backus–Naur form](https://en.wikipedia.org/wiki/Extended_Backus%E2%80%93Naur_form), with the exception of the `str(..)` function / parameterized rule, which length-prefixes and pads strings.
This makes the resulting binary format easier to parse.

Regular users do *not* need to know this information.
But for those interested in exactly how Nix works, e.g. if they are reimplementing it, this information can be useful.

```ebnf
nar = str("nix-archive-1"), nar-obj;

nar-obj = str("("), nar-obj-inner, str(")");

nar-obj-inner
  = str("type"), str("regular") regular
  | str("type"), str("symlink") symlink
  | str("type"), str("directory") directory
  ;

regular = [ str("executable") ], str("contents"), str(contents);

symlink = str("target"), str(target);

(* side condition: directory entries must be ordered by their names *)
directory = { directory-entry };

directory-entry = str("entry"), str("("), str("name"), str(name), str("node"), nar-obj, str(")");
```

The `str` function / parameterized rule is defined as follows:

- `str(s)` = `int(|s|), pad(s);`

- `int(n)` = the 64-bit little endian representation of the number `n`

- `pad(s)` = the byte sequence `s`, padded with 0s to a multiple of 8 byte

## Kaitai Struct Specification

The Nix Archive (NAR) format is also formally described using [Kaitai Struct](https://kaitai.io/), an Interface Description Language (IDL) for defining binary data structures.

> Kaitai Struct provides a language-agnostic, machine-readable specification that can be compiled into parsers for various programming languages (e.g., C++, Python, Java, Rust).

```yaml
meta:
  id: nix_nar
  title: Nix Archive (NAR)
  file-extension: nar
  endian: le
doc: |
    Nix Archive (NAR) format. A simple, reproducible binary archive
    format used by the Nix package manager to serialize file system objects.
doc-ref: 'https://nixos.org/manual/nix/stable/command-ref/nix-store.html#nar-format'

seq:
  - id: magic
    type: padded_str
    doc: "Magic string, must be 'nix-archive-1'."
    valid:
      expr: _.body == 'nix-archive-1'
  - id: root_node
    type: node
    doc: "The root of the archive, which is always a single node."

types:
  padded_str:
    doc: |
      A string, prefixed with its length (u8le) and
      padded with null bytes to the next 8-byte boundary.
    seq:
      - id: len_str
        type: u8
      - id: body
        type: str
        size: len_str
        encoding: 'ascii'
      - id: padding
        size: (8 - (len_str % 8)) % 8

  node:
    doc: "A single filesystem node (file, directory, or symlink)."
    seq:
      - id: open_paren
        type: padded_str
        doc: "Must be '(', a token starting the node definition."
        valid:
          expr: _.body == '('
      - id: type_key
        type: padded_str
        doc: "Must be 'type'."
        valid:
          expr: _.body == 'type'
      - id: type_val
        type: padded_str
        doc: "The type of the node: 'regular', 'directory', or 'symlink'."
      - id: body
        type:
          switch-on: type_val.body
          cases:
            "'directory'": type_directory
            "'regular'": type_regular
            "'symlink'": type_symlink
      - id: close_paren
        type: padded_str
        valid:
          expr: _.body == ')'
        if: "type_val.body != 'directory'"
        doc: "Must be ')', a token ending the node definition."

  type_directory:
    doc: "A directory node, containing a list of entries."
    seq:
      - id: entries
        type: dir_entry
        repeat: until
        repeat-until: _.kind.body == ')'
    types:
      dir_entry:
        doc: "A single entry within a directory, or a terminator."
        seq:
          - id: kind
            type: padded_str
            valid:
              expr: _.body == 'entry' or _.body == ')'
            doc: "Must be 'entry' (for a child node) or '' (for terminator)."
          - id: open_paren
            type: padded_str
            valid:
              expr: _.body == '('
            if: 'kind.body == "entry"'
          - id: name_key
            type: padded_str
            valid:
              expr: _.body == 'name'
            if: 'kind.body == "entry"'
          - id: name
            type: padded_str
            if: 'kind.body == "entry"'
          - id: node_key
            type: padded_str
            valid:
              expr: _.body == 'node'
            if: 'kind.body == "entry"'
          - id: node
            type: node
            if: 'kind.body == "entry"'
            doc: "The child node, present only if kind is 'entry'."
          - id: close_paren
            type: padded_str
            valid:
              expr: _.body == ')'
            if: 'kind.body == "entry"'
        instances:
          is_terminator:
            value: kind.body == ')'

  type_regular:
    doc: "A regular file node."
    seq:
      # Read attributes (like 'executable') until we hit 'contents'
      - id: attributes
        type: reg_attribute
        repeat: until
        repeat-until: _.key.body == "contents"
      # After the 'contents' token, read the file data
      - id: file_data
        type: file_content
    instances:
      is_executable:
        value: 'attributes[0].key.body == "executable"'
        doc: "True if the file has the 'executable' attribute."
    types:
      reg_attribute:
        doc: "An attribute of the file, e.g., 'executable' or 'contents'."
        seq:
          - id: key
            type: padded_str
            doc: "Attribute key, e.g., 'executable' or 'contents'."
            valid:
              expr: _.body == 'executable' or _.body == 'contents'
          - id: value
            type: padded_str
            if: 'key.body == "executable"'
            valid:
              expr: _.body == ''
            doc: "Must be '' if key is 'executable'."
      file_content:
        doc: "The raw data of the file, prefixed by length."
        seq:
          - id: len_contents
            type: u8
          # This relies on the property of instances that they are lazily evaluated and cached.
          - size: 0
            if: nar_offset < 0
          - id: contents
            size: len_contents
          - id: padding
            size: (8 - (len_contents % 8)) % 8
        instances:
          nar_offset:
            value: _io.pos

  type_symlink:
    doc: "A symbolic link node."
    seq:
      - id: target_key
        type: padded_str
        doc: "Must be 'target'."
        valid:
          expr: _.body == 'target'
      - id: target_val
        type: padded_str
        doc: "The destination path of the symlink."
```

The source of the spec can be found [here](https://github.com/fzakaria/nix-nar-kaitai-spec/blob/main/NAR.ksy). Contributions and improvements to the spec are welcomed.