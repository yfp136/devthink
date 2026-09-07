# 单元测试（规格 4.2 DoD：cmake --build --preset debug 后 ctest --preset debug 全绿）
# 每个用例是一个独立可执行文件：断言失败打印 [FAIL] 并以非 0 退出。
function(sm_add_unit_test name libs)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE ${libs})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# 纯逻辑内核（core + db）
sm_add_unit_test(test_envelope sm_core)          # 规格 5.1 信封解析/构造
sm_add_unit_test(test_op_dict sm_core)           # 规格 5.3/5.4 指令与事件字典
sm_add_unit_test(test_util sm_core)              # 工具：SHA-256/UUID/时间
sm_add_unit_test(test_msg_bus sm_core)           # 规格 5.2 总线路由与心跳
sm_add_unit_test(test_db sm_core)                # 规格 6.1/6.2 schema 与迁移
sm_add_unit_test(test_project sm_core)           # 规格 8.2 manifest + 10.4 state_json 校验

# 引擎注册表（含 ABI 头编译校验）
sm_add_unit_test(test_engine_registry sm_engines)

# 引擎空壳（Windows 插件 DLL 同源模板；本机 ABI 冒烟）
sm_add_unit_test(test_engine_stub sm_engines)

# Web 远控层（SHA-1/Base64/Auth/RemoteQueue）
sm_add_unit_test(test_web sm_web)

# M5 时间线调度器（§7 六轨调度 + §10.6 条目状态机）
sm_add_unit_test(test_timeline sm_engines)

# M2 媒体引擎（§10.2 状态机 + 增益/淡变包络 + 单源互斥）
sm_add_unit_test(test_media sm_engines)
