# Walkthrough - Display Subsystem Decoupling (`DisplayManager`)

Decoupled the display subsystem (I2C bus, SSD1306 hardware driver, Framebuffer graphics context, and `display_task`) from `HubApp` into a dedicated, autonomous `DisplayManager` component in `smart-farm-hub`.

## Changes Made

### 1. Component Interfaces & HAL Layer
- **[NEW] [`i_display_manager.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-hub/main/include/interfaces/i_display_manager.hpp)**: Defined `IDisplayManager` interface (`init()`, `start()`, `get_task_handle()`).
- **[NEW HAL] [`i_hal_i2c.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-hub/components/idf_hals/include/interfaces/i_hal_i2c.hpp)**, **[`hal_i2c.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-hub/components/idf_hals/include/hal_i2c.hpp)**, **[`hal_i2c.cpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-hub/components/idf_hals/src/hal_i2c.cpp)**, and **[`mock_hal_i2c.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-hub/components/idf_hals/mocks/mock_hal_i2c.hpp)**: Added 1:1 I2C Master HAL (`II2cHAL`) into `idf_hals` to eliminate direct C API driver calls outside HALs and enable host unit testing.

### 2. Display Subsystem (`DisplayManager`)
- **[NEW] [`display_manager.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-hub/main/include/display_manager.hpp)**:
  - Implements `IDisplayManager`.
  - Uses `std::optional<HalDisplaySsd1306>` and `std::optional<FramebufferGraphicsContext>` to ensure **zero dynamic heap allocation (`new`)**.
  - Configurable via `DisplayManagerConfig` (SDA/SCL pins, width, height, rotation, task priority/stack size).
- **[NEW] [`display_manager.cpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-hub/main/src/display_manager.cpp)**:
  - Initializes I2C master bus via `II2cHAL`.
  - Runs `display_loop()` task, processing UI input events from `ui_event_queue_`, posting app commands to `app_cmd_queue_`, and safely rendering `g_system_state` snapshots.

### 3. Refactored `HubApp` & `main.cpp`
- **[MODIFY] [`hub_app.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-hub/main/include/hub_app.hpp)** & **[`hub_app.cpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-hub/main/src/hub_app.cpp)**:
  - Removed all display/I2C includes, members, `init_display()`, and `display_task`.
  - Simplified `HubApp::init(config, app_cmd_queue)`.
- **[MODIFY] [`main.cpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-hub/main/main.cpp)**:
  - Instantiates `g_state_mutex`, `hal_i2c`, `DisplayManager`, and `HubApp`.
  - Starts `DisplayManager` and `HubApp` independently.
- **[MODIFY] [`CMakeLists.txt`](file:///home/german/dev/workspaces/smart-farm/smart-farm-hub/main/CMakeLists.txt)**:
  - Added `"src/display_manager.cpp"` to build sources.

---

## Verification Results

### Automated Build Verification
Compiled firmware target `esp32s3` with ESP-IDF toolchain:

```bash
cd /home/german/dev/workspaces/smart-farm/smart-farm-hub && source $HOME/dev/esp/esp-idf/export.sh && idf.py build
```

**Build Output:**
- `[1/9] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/src/display_manager.cpp.obj`
- `[2/9] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/main.cpp.obj`
- `[3/9] Building CXX object esp-idf/main/CMakeFiles/__idf_main.dir/src/hub_app.cpp.obj`
- `[6/9] Linking CXX executable hub.elf`
- `[8/9] Generated build/hub.bin (0x13b960 bytes, 65% free partition space)`
- **Exit Code:** `0` (Success)
