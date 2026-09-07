# SiWx917 超低功耗计划

2026-09-02 起草。目标板 SIWX917_AI_DEV_KIT（SiWG917M111MGTBA，封装内 8 MB PSRAM，APS6404L-SQH），WiSeConnect v4.0.0 + Simplicity SDK 2025.6.1。

## 一、结论先行

- **芯片本身能做到**：NWP 联网保活（DTIM10）65 µA，M4 睡眠保持 320 KB 13.7 µA，PS0 关机 1.44 µA。两颗核加起来 <100 µA 是数据手册背书的数字。
- **这块板子的天花板是 PSRAM**。数据手册 Table 5.10 对"封装内 PSRAM 常供电"方案给的估算是：深睡 350 µA、DTIM10 保活 390 µA、DTIM3 保活 435 µA。要进 µA 级必须让 PSRAM 断电或进 half-sleep，而当前固件的 .text/.bss/heap 全在 PSRAM 里，这决定了整个计划的路线。
- **现在 SDK 的低功耗是零**：tkl_sleep.c 四个接口全是空桩，tkl_wifi_set_lp_mode 返回 NOT_SUPPORTED，没有 tkl_wakeup.c，SLC 工程没有拉入 power manager。上层 tuya_pm / lpmgr / tal_cpu 这一整套框架已经在 src 里，只等 TKL 落地。
- **分四步走，每一步单独可交付**：先 NWP 联网省电（收益最大、风险最小），再 M4 tickless 睡眠，再 PS0 深睡加唤醒源，最后接 tuya_pm 做产品级验收。预期从当前约 40 到 60 mA 的空闲电流，第一步降到 15 mA 内，第二步降到 1 mA 内，第三步深睡看 PSRAM 能否断电，能则 <10 µA，不能则约 350 µA。

## 二、资料要点（Silicon Labs 官方）

### 2.1 M4 电源状态

- **PS4**：全功能，100/180 MHz。Active 12.7 mA，Sleep（320 KB 保持）13.7 µA。
- **PS3**：全功能，40/80 MHz，电压更低。Active 7.6 mA，Sleep 13.7 µA。
- **PS2**：只有 ULP/UULP 外设，**flash 断电，代码必须在 RAM 里跑**，20 MHz 固定。Active 808 µA，Sleep 13.7 µA。需要 SLI_SI91X_MCU_ENABLE_RAM_BASED_EXECUTION 和 PS2 组件。
- **PS1**：CPU 关，只剩 ULP 外设，从 PS2 进。309 µA。
- **PS0**：关机，不保持 RAM，唤醒等于数字域复位（软件重跑启动）。1.44 µA。
- **Standby 与 Sleep 的区别**：Standby 只是 WFI 门控时钟，任何 NVIC 中断能唤醒，唤醒快但底电流高；Sleep 才真正断电，只能靠下面列的唤醒源。

### 2.2 唤醒源（数据手册 Table 5.9）

- **Sleep / PS0 能用**：UULP VBAT GPIO（0 到 4 号）、SysRTC、Alarm（日历 RTC）、Deep-Sleep Timer、WDT、BOD。PS0 额外不能用 wireless。
- **只能唤 Standby、不能唤 Sleep**：ULP GPIO、ULP UART/I2C/SPI/I2S 中断、Comparator。
- **Wireless 中断**（NWP 唤 M4）：PS4/PS3/PS2 Standby 和 Sleep 都可以，PS0 不行。
- 对本板的含义：SW2/SW3 接在 UULP GPIO 2/3，可以做深睡唤醒键；SW1（ai_chat_button）接在 HP GPIO 49，**睡眠中按了没反应**，产品化要换脚或改用 SW2。

### 2.3 NWP（Wi-Fi 协处理器）省电模式

- **API**：`sl_wifi_set_performance_profile_v2()`，profile 取 HIGH_PERFORMANCE / ASSOCIATED_POWER_SAVE（Max PSP）/ ASSOCIATED_POWER_SAVE_LOW_LATENCY（Fast PSP）/ DEEP_SLEEP_WITH_RAM_RETENTION / DEEP_SLEEP_WITHOUT_RAM_RETENTION。
- **唤醒对齐**：dtim_aligned_type 选 SL_SI91X_ALIGN_WITH_DTIM_BEACON；listen interval 用 `sl_wifi_set_listen_interval_v2()`，入网前要在 join_feature_bitmap 里置 SI91X_JOIN_FEAT_LISTEN_INTERVAL_VALID。
- **Listen interval 上限 1000 ms**：Silabs 明确说超过 1000 ms 会被 AP 踢。按 100 ms beacon 就是 DTIM10；涂鸦 lpmgr 的 DTIM20/30 档在这颗芯片上要夹到 10。
- **数据手册电流**：Standby Associated DTIM10、30 s WLAN keepalive、352 KB 保持：65 µA（无 TCP keepalive）/ 73 µA（240 s TCP keepalive）。TWT 自动配置 22 到 97 µA（看 RX 延迟 2 s 到 60 s）。Listen 16.5 mA，RX 22 到 51 mA，TX 170 到 240 mA。深睡 2.5 µA（无 RAM）/ 10 µA（352 KB 保持）。
- **启动特性位**：SL_SI91X_EXT_FEAT_LOW_POWER_MODE、SL_SI91X_EXT_FEAT_XTAL_CLK（当前 tkl_wifi.c 已经带了），加 SL_SI91X_ENABLE_ENHANCED_MAX_PSP 提升 Max PSP 兼容性。
- **M4 与 NWP 握手**：四个标志 TA_wakeup_M4 / TA_is_Active / M4_wakeup_TA / M4_is_Active 在 P2P 状态寄存器里，由 power manager 内部维护，应用不碰。NWP 有包给 M4 时会通过 wireless 唤醒源把 M4 拉起来。

### 2.4 Power Manager 与 tickless

- **组件**：`sl_power_manager`（自动带 si91x_tickless_mode、wakeup_source_config、power_manager_config、sleeptimer_si91x）。加进 slcp 后 FreeRTOSConfig.h 里的 configUSE_TICKLESS_IDLE 会由 SL_SI91X_TICKLESS_MODE 宏自动打开，`sl_si91x_power_manager_init()` 通过 service_init 事件自动调用，初始状态 PS3。
- **机制**：需求计数。`sl_si91x_power_manager_add_requirement(PS4)` 期间不会掉到更低态；idle 任务里 `vPortSuppressTicksAndSleep()` 判断 `sl_si91x_power_manager_is_ok_to_sleep()`，条件是：无 PS 需求、NWP profile 不是 DEEP_SLEEP_WITHOUT_RAM_RETENTION、SDK 没有进行中的 TX / flash 命令、NWP 没有待处理的收包。默认阈值 configEXPECTED_IDLE_TIME_BEFORE_SLEEP 100 ms。
- **时钟**：tickless 用 SysRTC + sleeptimer 做 tick 源，Silabs 强调 SysRTC 不能再他用。
- **RAM 保持**：`sl_si91x_power_manager_configure_ram_retention()`，按大小或按 bank；睡眠路径 rsi_deepsleep_soc.c 在 SLEEP_WITH_RETENTION 时会调 `sl_si91x_psram_sleep()`（APS6404L-SQH 支持 half-sleep），醒来再 `sl_si91x_psram_wakeup()`。
- **已知问题**：4.1.1 release note 记录"从 4.0.3 升 4.1.0 后 NWP 不能正常睡眠"已修复。我们在 4.0.0，需要在第一步实测时留意 NWP 是否真的睡了；不行就得升 WiSeConnect。

### 2.5 参考例程（本地 sdks/wiseconnect/examples 里都有）

- featured/low_power/powersave_standby_associated：ASSOCIATED_POWER_SAVE_LOW_LATENCY + M4 sleep with retention，UDP 发包。
- featured/low_power/power_save_deep_sleep：NWP DEEP_SLEEP_WITHOUT_RAM_RETENTION + Alarm 定时唤醒。
- featured/low_power/twt_tcp_client：TWT。
- si91x_soc/service/sl_si91x_power_manager_tickless_idle：PS4/PS3/PS2 sleep 状态机，附 power_manager_integration.pdf。
- si91x_soc/mcu_powerstate/sl_si91x_ps0_state 等五个：单个电源态。

## 三、SDK 现状盘点

### 3.1 TKL 适配层（platform/SiWx917/tuyaos_adapter）

- **tkl_sleep.c**：`tkl_cpu_sleep_callback_register` / `tkl_cpu_sleep_mode_set` 返回 NOT_SUPPORTED，`tkl_cpu_allow_sleep` / `tkl_cpu_force_wakeup` 空函数。
- **tkl_wifi.c**：`tkl_wifi_set_lp_mode` 返回 NOT_SUPPORTED。`_tkl_wifi_set_high_performance()` 在 init、scan、connect、scan 重试四处强制 HIGH_PERFORMANCE，没有恢复省电的对称调用。boot_config 已带 LOW_POWER_MODE / XTAL_CLK / ULP_GPIO_BASED_HANDSHAKE，缺 ENHANCED_MAX_PSP。TCP/IP 走 SL_SI91X_TCP_IP_FEAT_BYPASS（lwIP 在 M4 上跑）。
- **tkl_wakeup.c 不存在**，tdd_power_soc.c 调的 `tkl_wakeup_source_set` 在本平台链接不到。T5AI 有现成的 tkl_wakeup.h 可对照。
- **tkl_system.c**：`tkl_system_get_reset_reason` 返回 UNSUPPORT。毫秒时钟 `tkl_system_get_millisecond` 用 ULP timer0 做 100 s 周期计数（TKL_ULP_TIMER_SYSTICK_ENABLE）；tickless 之后 M4 睡着时这个计数器是否走、g_mstick 要不要补偿，需要验证。
- **tkl_rtc.c** 用 NPSS calendar，和 Alarm 唤醒源是同一块，不冲突。
- **tkl_timer.c** 四路 ULP timer 全部占用，其中 0 号被 systick 占。
- **tkl_bt.c** 有 `tkl_hci_deinit`，BLE coex 默认常开（SL_SI91X_WLAN_BLE_MODE）。

### 3.2 平台工程（slc / mcu / linker）

- **slcp 模板**没有 sl_power_manager、si91x_tickless_mode、wakeup 源组件；已有 sleeptimer_si91x、sl_ulp_timer、sl_clock_manager。
- **app_tuya.c**：启动把 M4 拉到 180 MHz SOC PLL，QSPI 也挂 PLL；已做 `sl_si91x_m4_ta_secure_handshake(SL_SI91X_ENABLE_XTAL)`。
- **linkerfile_psram_SoC.ld** 早有预留：把 power_manager、tickless、sysrtc、sleeptimer、deepsleep、psram、qspi、pll、egpio 这些对象排除在 PSRAM .text 之外，其中 deepsleep/psram/qspi/pll/egpio 直接放进片内 SRAM。也就是唤醒后重新点亮 PSRAM 的那段代码不依赖 PSRAM 本身。这对第二步是好消息。
- **PSRAM 配置**：psram_aps6404l_sqh，SLI_SI91X_MCU_EXTERNAL_LDO_FOR_PSRAM，PSRAM_HALF_SLEEP_SUPPORTED=1。
- **wiseconnect.patch** 只改了 power manager 的两行 DEBUGOUT，没动睡眠逻辑。
- **日志**走 ULP UART（ULP_GPIO_9/11），PS2 也能用，但 ULP UART 中断唤不醒 Sleep。

### 3.3 上层框架（src，共用，不改）

- **tuya_pm**：ACTIVE / CEC_T20（DTIM1）/ ULP_ONLINE（DTIM10）/ DEEPSLEEP 四档方案，锁 + 消费者 + 空闲衰减。ULP_ONLINE 走 lpmgr → tal_wifi_lp_enable → `tkl_wifi_set_lp_mode(TRUE, dtim)` + tal_cpu_lp_enable → `tkl_cpu_sleep_mode_set(TRUE, TUYA_CPU_SLEEP)`。DEEPSLEEP 走 tdd_power_soc → `tkl_wakeup_source_set` + `tkl_cpu_sleep_mode_set(TRUE, TUYA_CPU_DEEP_SLEEP)`。
- **lpmgr** 的 DTIM 表有 10/20/30 三档。
- **your_chat_bot** 已有 app_lowpower.c 用 tuya_pm（ZECTRIX_T5AI_NOTE_4 已开），SIWX917_AI_DEV_KIT.config 未开 ENABLE_APP_LOWPOWER，是现成的验收载体。
- **MQTT keepalive** 120 s（tuya_config_defaults.h）。

### 3.4 架构层面的两个硬约束

- **lwIP bypass 模式**：TCP/IP 栈在 M4，NWP 不会代发 TCP keepalive，也不会替 M4 回 ARP。每一个到站的单播/广播/组播帧都要把 M4 从睡眠里拉起来。家庭网络的 ARP/mDNS/SSDP 广播能让 M4 每秒醒好几次。缓解手段是 `sl_wifi_filter_broadcast()` 让 NWP 丢广播，以及 MQTT keepalive 拉长；根治要切到 NWP 内置 TCP/IP（offload），那是 tkl_network 和 lwIP 的大改，本计划不做，只记录。
- **代码在 PSRAM**：PS2 要求代码在 RAM 且关 flash，跟现在 flash + PSRAM 的布局不兼容，所以 M4 侧只做 PS4/PS3 sleep with retention，不做 PS2/PS1。PS0 深睡等于重启，不受影响。

## 四、实施计划

### Phase 0：测量基线（1 天）

- 确认板子的电流测点。AI dev kit 没有 WPK 的 AEM 电流通道，需要在 VBAT 或 3.3 V 输入串功耗仪（PPK2 / Joulescope 均可）。
- 记录 your_chat_bot 当前空闲电流、Wi-Fi 联网后电流、屏和背光关掉后电流，作为对照。
- 把 `sl_si91x_power_manager_tickless_idle` 和 `powersave_standby_associated` 两个原厂例程在同一块板上跑通并测出数字，确认 4.0.0 固件 + 这块板的 NWP 能睡、PSRAM half-sleep 后板级底电流是多少。**这个数字决定 Phase 3 的目标该定 10 µA 还是 350 µA。**
- 产出：一张基线表，写进本文档第六节。

### Phase 1：NWP 联网省电（tkl_wifi_set_lp_mode）（2 到 3 天）

改动都在 platform/SiWx917/tuyaos_adapter/src/tkl_wifi.c：

- `tkl_wifi_set_lp_mode(enable, dtim)`：enable 时 profile = ASSOCIATED_POWER_SAVE（dtim ≥ 3）或 ASSOCIATED_POWER_SAVE_LOW_LATENCY（dtim 1、2），dtim_aligned_type = ALIGN_WITH_DTIM_BEACON，listen_interval = min(dtim, 10) × 100 ms；disable 时 HIGH_PERFORMANCE。记住最后一次 enable 的参数。
- `_tkl_wifi_set_high_performance()` 四个调用点之后，在 scan 结束和 connect 成功回调里恢复上一次的省电设置，否则 tuya_pm 开了低功耗、一次重连就被悄悄关掉。
- boot_config 加 SL_SI91X_ENABLE_ENHANCED_MAX_PSP；join_feature_bitmap 加 SI91X_JOIN_FEAT_LISTEN_INTERVAL_VALID。
- 联网成功后调 `sl_wifi_filter_broadcast()`，阈值取 5000 ms、丢非 ARP 广播，实测 DHCP renew 和 mDNS 不受影响再定。
- BLE：ULP_ONLINE 时 tuya_pm 已经会调 `tuya_ble_deinit`，确认 tkl_hci_deinit 后 NWP 的 BLE 广播真的停了（看电流）。
- 验收：M4 保持 active，只开 NWP 省电，整机电流从基线降约 15 mA（listen 16.5 mA → 65 µA 量级）；MQTT 保持在线 12 小时不掉；App 下发控制延迟在 DTIM10 下 ≤1.5 s。

### Phase 2：M4 tickless 睡眠（PS4/PS3 sleep with retention）（1 到 2 周）

- **工程**：slcp 模板加 `sl_power_manager` 和 `wireless_wakeup_ulp_component`；wakeup_source_config 打开 SL_ENABLE_WIRELESS_WAKEUP_SOURCE（默认已开）和 SysRTC。SLI_SI91X_MCU_MOV_ROM_API_TO_FLASH 已定义，注意 flash 里的 ROM API 在睡眠前后要可访问（这也是为什么不碰 PS2）。
- **tkl_sleep.c**：
  - `tkl_cpu_sleep_mode_set(TRUE, TUYA_CPU_SLEEP)` = remove PS4 requirement，允许 idle 进 sleep；`(FALSE, …)` = add PS4 requirement。用计数配合 tal 层的 disable_cnt。
  - `tkl_cpu_allow_sleep` / `tkl_cpu_force_wakeup` 映射到同样的 requirement 增减。
  - `tkl_cpu_sleep_callback_register`：挂到 `sl_si91x_power_manager_subscribe_ps_transition_event`，LEAVING_SLEEP 时回调 wakeup，ENTERING 时回调 sleep。
- **时间源**：把 `tkl_system_get_millisecond` 改为 `sl_sleeptimer_get_tick_count64()` 折算，或在 LEAVING_SLEEP 回调里用 SysRTC 差值补偿 g_mstick。前者干净，还能把 ULP timer0 让出来。需要确认 tal_sw_timer 和 MQTT 心跳在睡一觉后不误判超时。
- **外设需求**：睡眠期间 ULP UART 只能唤 standby，所以日志打印期间要持有 PS4 requirement（tkl_uart 写入时加、DMA/FIFO 空后减）。SPI 屏、I2S 音频、PWM 背光在活动时同样持有 requirement；tuya_pm 的 consumer suspend/resume 天然对应这一加一减。
- **PSRAM**：验证 half-sleep 进出后 PSRAM 里的 .text/.bss/heap 完整（跑一遍 crc），并测 half-sleep 后的实际电流。如果 half-sleep 电流仍在数百 µA，这就是本板的下限，记录下来交硬件评审。
- **RAM 保持**：`sl_si91x_power_manager_configure_ram_retention()` 按实际 SRAM 用量取 bank；linker 里 ram 是 0x4f800（318 KB）基本全用，就配 320 KB。
- **验收**：Phase 1 基础上，屏灭、无对话、MQTT 在线，平均电流 <1 mA（PSRAM 允许的话），按键/下发唤醒后功能正常，24 小时无重启。

### Phase 3：PS0 深睡与唤醒源（tkl_wakeup.c / DEEP_SLEEP）（1 周）

- **新建 tkl_wakeup.c** 和 include/system/tkl_wakeup.h（对照 T5AI 的接口）：
  - GPIO 源：只接受 TUYA_GPIO_NUM_0 到 4（UULP），映射 `sl_si91x_power_manager_set_wakeup_sources(GPIO_WAKEUP)` + NPSS GPIO 极性配置；其余引脚返回 INVALID_PARM 并打日志，避免产品端悄悄配了个唤不醒的键。
  - TIMER / RTC 源：映射 Alarm（calendar）或 Deep-Sleep Timer，ms 转秒级 alarm。
- **tkl_cpu_sleep_mode_set(TRUE, TUYA_CPU_DEEP_SLEEP)**：先 `sl_wifi_set_performance_profile_v2(DEEP_SLEEP_WITHOUT_RAM_RETENTION)`（或先 `sl_wifi_deinit`），再 remove 全部需求让 power manager 进 PS0；不返回。
- **tkl_system_get_reset_reason**：用 NPSS 唤醒状态寄存器区分冷启 / PS0 唤醒 / 看门狗，tuya_pm 与应用据此决定要不要跳过开机动画。
- **对接 tdd_power_soc**：把板级 SW2/SW3 声明为 wakesrc，验证 `tdl_power_enter_deepsleep` 一条链走通。
- **验收**：深睡电流（PSRAM 常供约 350 µA，若硬件能断则 <10 µA），按键唤醒到 MQTT 在线 <5 s，定时唤醒准确。

### Phase 4：接 tuya_pm 做产品级验收（1 周）

- SIWX917_AI_DEV_KIT.config 打开 ENABLE_TUYA_PM、ENABLE_WIFI_ULTRA_LOWPOWER、ENABLE_APP_LOWPOWER、APP_LOWPOWER_AP_KEEPALIVE。
- lpmgr 的 DTIM20/30 在本平台夹到 10（放在 tkl_wifi_set_lp_mode 里做，不动 src）。
- 三个场景各跑 24 小时并画电流曲线：ACTIVE 对话、ULP_ONLINE 保活、DEEPSLEEP 定时醒。
- 可选增强：TWT（`sl_wifi_target_wake_time_auto_selection`，需 Wi-Fi 6 AP，可把保活降到 22 到 35 µA）；PS3 降频（`sl_si91x_power_manager_set_clock_scaling`，要重算 QSPI/PSRAM 时钟分频）。
- 更新 GETTING_STARTED.md 低功耗章节和本文档第六节实测表。

## 五、风险与待验证项

- **PSRAM 底电流**：全计划最大的不确定量，Phase 0 必须先测。若 half-sleep 后仍 >300 µA，"超低功耗"在这块板只能到亚毫安，µA 级要等能断 PSRAM 的硬件（数据手册 Option 4）。
- **广播风暴唤醒 M4**：bypass 模式的固有问题，只靠 NWP 广播过滤。要在真实家庭网络（有手机、电视、打印机）测，不能只在隔离 AP 下测。
- **4.0.0 NWP 不睡**：4.1.1 修过同类问题。Phase 0 原厂例程测不出 65 µA 量级就要评估升级 WiSeConnect，升级会牵动 mcu/patch/wiseconnect.patch。
- **时间跳变**：tickless 后 ULP timer 计数和 FreeRTOS tick 的关系变了，tal_time、MQTT 超时、tal_sw_timer 都可能受影响，Phase 2 要有专项回归。
- **OTA / flash 写期间禁睡**：SDK 的 is_ok_to_sleep 已经查 flash 命令状态，但 littlefs 写 tal_kv 时要确认也在保护内。
- **SW1 在 HP GPIO**：睡眠中唤不醒，产品化要么换 UULP 脚，要么接受只有 SW2/SW3 能唤。
- **BLE coex**：常开 BLE 广播本身 37 到 41 µA，ULP_ONLINE 关掉后要确认配网流程还能重新拉起。
- **不动 src**：所有改动落在 platform/SiWx917 和 boards/SiWx917，lpmgr DTIM 夹值等平台差异也在 TKL 里吞掉。

## 六、实测记录

Phase 0 起逐项填写：场景、profile、DTIM、M4 状态、平均电流、峰值、测量条件。

## 七、参考

- Silicon Labs AN1430 SiWG917 Low Power Application Note，NWP Power Save Modes：https://docs.silabs.com/wiseconnect/latest/wifi-siwx917-ble-low-power-application-note/nwp-power-save-modes
- AN1508 Power Manager Application Note，Tickless Idle：https://docs.silabs.com/wiseconnect/4.0.1/wifi-siwx917-soc-power-manager/power-manager-with-tickless-idle
- AN1515 M4 Sleep Application Note，M4 Power Modes：https://docs.silabs.com/wiseconnect/4.0.0/siwg917-m4-sleep/m4-power-modes
- Power Manager Developer Guide，Architecture：https://docs.silabs.com/wifi-developer-guides/latest/power-manager-developer-guide/power-manager-architecture
- AN1433 NCP Low Power，Current Consumption：https://docs.silabs.com/wiseconnect/4.1.1/wifi-siwx917-ncp-low-power-application-note/current-consumption
- SiWG917 Datasheet Rev 1.2（Table 5.9 唤醒源、Table 5.10 PSRAM 电流估算、Table 7.62 到 7.64 电流）：https://www.silabs.com/documents/public/data-sheets/siwg917-datasheet.pdf
- WiSeConnect Release Notes：https://docs.silabs.com/wiseconnect/latest/sisdk-wifi-release-notes/
- WF-301 Designing Low Power Applications with Wi-Fi 6：https://www.silabs.com/documents/public/presentations/wf-301-designing-low-power-iot-applications-with-wi-fi-6-and-siwx917.pdf
- Silabs 南欧 FAE 团队 SiWG917 sleep 软件指南：https://jerome-silabs.github.io/SE_FAE_team/SiWG917/Guides/Sleep/software.html
