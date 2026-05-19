# Config Descriptor Codegen

This file describes config metadata codegen. The goal is to avoid usage of macros and template reflection in config to speed up compilation, but preserve singe-line source of truth for configuration langauge. 

The run-time configuration structures are going to be describe in src/config/config_desc.h file. It will be cpp-like file that is not actually used by the problem source code. As source-code generation tool (tools\/config_meta_gen.py) will read this file and will produce a generated config/config_meta.h and config/config_meta.cpp files. The config/config_meta.h is then included into config/config.h and brings pre-generated metadata-rich headers into all the users which include config/config.h. 

# config_desc.h

This file uses a strict subset of C++ that config_desc_gen tool recognized and one-line source-code comments before each struct declare that it is a special limited syntax. It only contains structs that need metadata, but nothing else. Example:

```
// [fonts] section: syntax supported by config_meta_gen. 
struct UiFontSetConfig {
    UiFont title;
    UiFont big;
    UiFont value;
    UiFont label;
    UiFont text;
    UiFont small;
    UiFont footer;
    UiFont clockTime;
    UiFont clockDate;
};
```

It enforce strict correspondence between field names and config parameters (clockTime becomes "clock_time", small becomes "small", so a code change from smallText to small will be needed). It generates all fields as ediables and table the section name from the comment before the struct.

Dynamic section are declared like this (the $ in the name gives it away):

```
// [theme.$name] section: syntax supported by config_meta_gen. 
struct ThemeConfig {
    std::string name;
    std::string description;
    ColorConfig background;
    ColorConfig foreground;
    ColorConfig accent;
    ColorConfig guide;
};
```

Nested sections declared like this, config/dynamic destinction is automatically detected:

```
struct LayoutConfig {
    ColorsConfig colors;
    LayoutGuideSheetConfig layoutGuideSheet;
    ThemeConfig themes name;
    DashboardSectionConfig dashboard;
    CardStyleConfig cardStyle;
    MetricListWidgetConfig metricList;
    DriveUsageListWidgetConfig driveUsageList;
    ThroughputWidgetConfig throughput;
    GaugeWidgetConfig gauge;
    TextWidgetConfig text;
    NetworkFooterWidgetConfig networkFooter;
    LayoutEditorConfig layoutEditor;
    UiFontSetConfig fonts;
    BoardConfig board;
    MetricsSectionConfig metrics;
    LayoutCardConfig cards;
    LayoutSectionConfig layouts;

    // %custom_fields% config_meta_gen syntax
    LayoutSectionConfig structure{};
    LayoutNodeConfig cardsLayout;
};
```

Note "%custom_fields%" comment.

# config_meta.h

Includes all the structures declared in config_desc.h and all generated metadata, with xxxMeta structures for all fields and root binging from Cofig to leaves. config_meta.cpp will own the corresponding declarations to make .h files as small as possible.
