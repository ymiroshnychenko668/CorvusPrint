# wxWidgets Dropdown Controls Reference

## Summary

This document catalogs all dropdown controls (wxComboBox, wxChoice, ComboBox wrappers) found in the GUI codebase.

---

## Dropdowns by File

### ExtrusionCalibration.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `m_comboBox_nozzle_dia` | Nozzle Diameter | 0.2, 0.4, 0.6, 0.8 (mm) | 66-73 |
| `m_comboBox_filament` | Filament | Dynamic (from presets) | 82-85 |
| `m_comboBox_bed_type` | Bed Type | Dynamic (from config enum_keys_map) | 94-107 |

### AMSMaterialsSetting.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `m_comboBox_filament` | Filament | Dynamic (from presets) | 156-158 |
| `m_comboBox_cali_result` | Calibration Result | Dynamic | 138 |

### PrintHostDialogs.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `combo_groups` | File Group (Repetier) | Dynamic (from groups parameter) | 42 |

### ConfigWizard.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `gcode_picker` | Firmware Type | Dynamic (from gcode_opt.enum_labels) | 1250 |

### Widgets/MultiNozzleSync.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `m_standard_choice` | Standard Nozzle Count | 0 to max_nozzle_count | 47 |
| `m_highflow_choice` | High Flow Nozzle Count | 0 to max_nozzle_count | 58 |

### Jobs/SLAImportJob.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `m_import_dropdown` | Import Mode | "Import model and profile", "Import profile only", "Import model only" | 52-54 |
| `m_quality_dropdown` | Quality Level | "Accurate", "Balanced", "Quick" | 65-67 |

### Tab.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `m_presets_choice` | Printer/Filament/Profile Presets | Dynamic (from PresetBundle) | 207 |

### PlateSettingsDialog.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `m_bed_type_choice` | Plate Bed Type | "Same as Global Plate Type", bed type enums | 383-401 |
| `m_print_seq_choice` | Print Sequence | "Same as Global Print Sequence", print_sequence enums | 410-413 |
| `m_spiral_mode_choice` | Spiral Mode | "Same as Global", "Enable", "Disable" | 421-425 |
| `m_first_layer_print_seq_choice` | First Layer Print Sequence | "Auto", "Customize" | 433-434 |
| `m_other_layer_print_seq_choice` | Other Layer Print Sequence | "Auto", "Customize" | 186-187 |

### PrintOptionsDialog.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `nozzle_type_checkbox` | Nozzle Type Selection | Dynamic (from device config) | 29 |
| `nozzle_diameter_checkbox` | Nozzle Diameter Selection | Dynamic (from device config) | 30 |
| `nozzle_flow_type_checkbox` | Nozzle Flow Type | Dynamic (from device config) | 33 |
| `multiple_left_nozzle_type_checkbox` | Left Nozzle Type (Multi) | Dynamic (from device config) | 39 |
| `purgechutepileup_detection_level_list` | Purge Chute Detection Sensitivity | Detection sensitivity levels | 754-759 |
| `nozzleclumping_detection_level_list` | Nozzle Clumping Detection Sensitivity | Detection sensitivity levels | 795-800 |
| `airprinting_detection_level_list` | Air Printing Detection Sensitivity | Detection sensitivity levels | 811+ |

### AMSSetting.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `m_type_combobox` | AMS Firmware Type | Dynamic (from AMS firmware m_name) | 607-668 |

### CaliHistoryDialog.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `m_comboBox_nozzle_dia` | Nozzle Diameter | "0.2 mm", "0.4 mm", etc. | 246 |

### CalibrationWizardPresetPage.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `m_comboBox_nozzle_dia` | Nozzle Diameter | "0.2 mm", "0.4 mm", etc. | 826 |
| `m_comboBox_nozzle_volume` | Nozzle Volume Type | Nozzle volume type enums (localized) | 834 |
| `m_comboBox_bed_type` | Bed Type | Bed type enums (localized) | 868 |
| `m_comboBox_process` | Process/Profile | Dynamic preset values | 201-230 |
| `m_left_comboBox_nozzle_dia` | Left Nozzle Diameter (Multi) | Formatted nozzle diameter list | 879 |
| `m_left_comboBox_nozzle_volume` | Left Nozzle Volume Type (Multi) | Nozzle volume type enums | 887 |
| `m_right_comboBox_nozzle_dia` | Right Nozzle Diameter (Multi) | Formatted nozzle diameter list | 898 |
| `m_right_comboBox_nozzle_volume` | Right Nozzle Volume Type (Multi) | Nozzle volume type enums | 906 |

### Field.hpp/Field.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `Choice` (window) | Config Option Choice | Dynamic (from m_opt.enum_labels or m_opt.enum_values) | 1192-1268 |

### SelectMachine.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `m_comboBox_printer` | Printer Selection | Dynamic printer list | 584, 5711 |

### SendToPrinter.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `m_comboBox_printer` | Printer Selection | Dynamic | 82 |
| `m_comboBox_bed` | Bed Selection | Dynamic | 83 |

### PresetComboBoxes.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `PresetComboBox` | Preset Selection (Generic) | Dynamic preset items with bitmap icons | 35+ |
| `PlaterPresetComboBox` | Filament/Printer Presets (Plater) | Dynamic with color picker support | 186-219 |
| `TabPresetComboBox` | Tab Presets | Dynamic preset list per type | 226-248 |
| `CalibrateFilamentComboBox` | Calibration Filament Selection | Dynamic with tray support | 254-282 |

### BitmapComboBox.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `BitmapComboBox` | Bitmap-based Dropdown | Dynamic items with bitmap icons | 16-61 |

### Plater.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `combo_printer_bed` | Printer Bed Type | Dynamic bed types from printer config | 2907-2916 |

### ObjColorDialog.cpp

| ID/Variable | Label/Purpose | Options | Line |
|-------------|---------------|---------|------|
| `bitmap_combox` | Color Palette Selection | Indexed numbers with bitmap icons | 32, 677 |

---

## Custom Wrapper Classes

### ComboBox (Widgets/ComboBox.hpp/cpp)
Custom wxWidgets wrapper extending TextInput with DropDown functionality.

**Methods:**
- `Append()` - Add items
- `SetItems()` - Set all items
- `SetSelection()` - Select item by index
- `GetSelection()` - Get selected index
- `GetValue()` - Get selected value

**Features:** Bitmap support, groups, tooltips, aliases

### Choice (Field.hpp/cpp)
Custom field wrapper for config option dropdowns.

- Extends Field base class
- Auto-populates from `enum_labels`/`enum_values`
- Supports icon rendering from resource files

### PresetComboBox (PresetComboBoxes.hpp/cpp)
Specialized combo for preset selection.

**Inheritance:**
```
ComboBox
  └── PresetComboBox
        ├── PlaterPresetComboBox
        ├── TabPresetComboBox
        └── CalibrateFilamentComboBox
```

Dynamic loading from PresetBundle.

### BitmapComboBox (BitmapComboBox.hpp/cpp)
wxBitmapComboBox wrapper for bitmap-supported dropdowns.

---

## Total Count: 45+ Dropdown Controls
