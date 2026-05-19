# Config Metadata Generation

`tools/config_meta_gen.py` generates CaseDash config schema structs, runtime config field metadata, a review manifest, and layout-edit field metadata from `src/config/config_desc.h`. The descriptor is the maintained source for schema-shaped config structs, section names, dynamic-section prefixes, field keys, field policies, and generated layout-edit config-field mappings.

`src/config/config.h` owns hand-authored value types and codec-owned payloads, then includes `config/config_meta.generated.h` for generated schema structs. `ColorConfig`, `UiFontConfig`, `LogicalPointConfig`, `LogicalSizeConfig`, `LayoutNodeConfig`, `MetricDefinitionConfig`, `BoardConfig`, and `MetricsSectionConfig` stay hand-authored because they carry behavior or storage that is not a fixed field list.

## Build Integration

CMake runs the generator as a custom command during normal builds. The command depends on `tools/config_meta_gen.py`, `src/config/config_desc.h`, and `resources/config.ini`, so CMake regenerates metadata when the generator, descriptor, or embedded config template changes.

The generator writes these files under `build/cmake/generated/`:

- `config/config_meta.generated.h`
- `config/config_meta.generated.cpp`
- `config/config_meta.generated.json`
- `layout_model/layout_edit_parameter_metadata.generated.h`
- `layout_model/layout_edit_parameter_metadata.generated.cpp`

Generated C++ is compiled into the app, tests, and benchmarks. The JSON manifest records sections, dynamic sections, custom codecs, editable fields, and layout-edit parameter mappings for review and tooling. The generator writes outputs only when content changes, which keeps rebuilds focused.

## Descriptor Source

`src/config/config_desc.h` is a C++-like descriptor file. It is readable beside the real config source but is parsed by the generator instead of compiled directly. The parser is intentionally line-oriented and accepts only the descriptor shape used by the project:

```text
descriptor        := (blank | comment | include | namespace_alias | struct_decl)*
include           := "#include" quoted_path
namespace_alias   := "namespace" identifier "=" qualified_identifier ";"
struct_decl        := struct_directive? "struct" type_name "{" field_decl* "};"
struct_directive  := "// config_meta:" struct_kind
struct_kind       := "static" section_name
                   | "dynamic_section" section_pattern "key=" identifier
                   | "custom_section" section_name "codec=" codec_name
                   | "container"
                   | "root"
field_decl        := field_type identifier ";" field_directive?
field_type        := qualified_identifier | "std::vector<" qualified_identifier ">"
field_directive   := "// config_meta:" field_attr+
field_attr        := "runtime_only" | "policy=" policy_name | "rename=" field_key
policy_name       := "none" | "positive_int" | "non_negative_int" | "font_size" | "degrees"
section_pattern   := "[" literal_prefix "$" identifier "]"
section_name      := "[" literal "]"
codec_name        := "board" | "metrics"
```

Field keys default to snake case derived from lower-camel C++ member names. `rename=` supplies the persisted key when a source member needs a different spelling. `FontsConfig::smallText` maps to the `[fonts] small` key this way, keeping the Win32 RPC `small` token out of the config schema.

## Section Models

Static sections map one generated struct to one persisted section:

```cpp
// config_meta: static [fonts]
struct FontsConfig {
    UiFontConfig title;
    UiFontConfig smallText;  // config_meta: rename=small
};
```

Dynamic sections map a vector item type to a section prefix and string key field:

```cpp
// config_meta: dynamic_section [theme.$name] key=name
struct ThemeConfig {
    std::string name;
    std::string description;
    ColorConfig background;
};
```

The dynamic key member comes from the section suffix, so it is not emitted as a configurable field. The generated section descriptor includes callbacks to ensure an item during parsing, find an item by key, and enumerate existing items during writing.

Custom sections reserve section order and codec dispatch for hand-authored parsers:

```cpp
// config_meta: custom_section [board] codec=board
struct BoardConfig {};

// config_meta: custom_section [metrics] codec=metrics
struct MetricsSectionConfig {};
```

`[board]` and `[metrics]` do not receive generic field tables because board sensor bindings and metric definitions have codec-owned key spaces.

Container and root structs describe ownership and traversal order. `AppConfig` is the only root, and nested owners such as `LayoutConfig` use `container`. `runtime_only` fields stay in the generated schema struct but are excluded from runtime field metadata, writer output, the JSON manifest, and layout-edit field metadata.

## Generated Schema Structs

`config_meta.generated.h` defines schema structs for non-custom descriptor entries. Fields are default-initialized, and each generated struct has a defaulted equality operator. Custom section structs are skipped because their storage is hand-authored in `src/config/config.h`.

The generated header contains data layout only. Parsing, encoding, clamping, color formatting, layout-expression formatting, and field comparison stay in hand-authored config code.

## Runtime Metadata

`config_meta.generated.cpp` emits a field array for each static and dynamic section, then flattens the root ownership tree into one runtime section descriptor table. The table preserves descriptor traversal order and records:

- Section name or dynamic prefix.
- Root offset for the owning struct.
- Section kind: static, dynamic, or custom.
- Codec kind: structured, board, or metrics.
- Field span for structured sections.
- Dynamic item size, key offset, and callbacks for dynamic sections.

Public runtime consumers use:

- `RuntimeConfigSectionDescriptors()`
- `RuntimeConfigFields(section)`
- `FindRuntimeConfigSection(sectionName)`
- `FindRuntimeConfigDynamicSection(sectionName)`
- `FindRuntimeConfigSectionByName(sectionName)`

`RuntimeConfigFieldDescriptor` records key text, owner offset, value kind, and clamp policy. `DecodeRuntimeConfigField`, `EncodeRuntimeConfigField`, and `RuntimeConfigFieldEquals` interpret those descriptors for parser, writer, save comparison, and layout-edit mutation paths. Shared non-template enums such as `configschema::ValueFormat` live in hand-authored headers; generated files provide the table data.

## Layout-Edit Metadata

The generator emits layout-edit config-field metadata separately from the runtime section table because layout edit needs direct `AppConfig` root offsets indexed by `LayoutEditParameter`.

`tools/config_meta_gen.py` keeps the active layout-edit field list in `ACTIVE_PARAMETERS`. Each entry names a `LayoutEditParameter` enum value, a static-section owner type, and a descriptor field. The generated table follows that list order, and the generated `.cpp` contains a `static_assert` that its row count matches `LayoutEditParameter::Count`. `src/widget/layout_edit_parameter_id.h` remains the authoritative enum and hit-test priority contract, so the enum order and `ACTIVE_PARAMETERS` order stay aligned.

Generated layout-edit rows include section name, persisted field key, value format, runtime value kind, clamp policy, and root offset. Layout edit reads the table through:

- `GetLayoutEditParameterInfo`
- `GetLayoutEditConfigFieldMetadata`
- `FindLayoutEditParameterByConfigField`
- `FindLayoutEditTooltipDescriptor`

Hand-authored layout-edit code uses this metadata for hit targets, tooltip config paths, numeric and color edits, font edits, and dirty comparison.

## Consistency Checks

The generator rejects malformed descriptors before writing outputs. It checks duplicate structs, duplicate sections, duplicate field keys, missing dynamic key fields, unknown field types, unknown field policies, unknown custom codecs, unsupported owner links, invalid `rename=` keys, and `runtime_only` fields that also declare `rename=`.

The generator also compares generated section and field spellings with `resources/config.ini`. Static sections must match the template section keys exactly. Dynamic sections must have at least one representative template section, and the union of representative keys must match the descriptor fields. Custom sections must be present in the template so generated section order stays aligned with the embedded config.
