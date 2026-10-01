# PC 与设备共用执行配置，避免拿不同安全配置的结果相互替代。
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 0)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_FAST_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_LIBC_UVWASI 0)
set(WAMR_BUILD_LIB_WASI_THREADS 0)
set(WAMR_BUILD_LIB_PTHREAD_SEMAPHORE 0)
set(WAMR_BUILD_LIB_PTHREAD 0)
set(WAMR_BUILD_THREAD_MGR 0)
set(WAMR_BUILD_SHARED_MEMORY 0)
set(WAMR_BUILD_MULTI_MODULE 0)
set(WAMR_BUILD_MINI_LOADER 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_REF_TYPES 0)
set(WAMR_BUILD_STRINGREF 0)
set(WAMR_BUILD_GC 0)
set(WAMR_BUILD_MEMORY64 0)
# bulk memory 按一个 opcode 计量任意长度 memcpy/memset；MVP 禁止以约束回调工作量。
set(WAMR_BUILD_BULK_MEMORY 0)
# WAMR 2.4.5 的 ExtraCommon 否则为空 struct，MSVC 标准 C 拒绝。
# 仅添加固定 8 个宿主上下文指针，不开放 guest 指令、导入或回调。
set(WAMR_BUILD_MODULE_INST_CONTEXT 1)
set(WAMR_BUILD_INSTRUCTION_METERING 1)
# 让线性内存也经过本实验的分配回调，避免漏计 os_mmap 路径。
set(WAMR_BUILD_ALLOC_WITH_USAGE 1)
set(WAMR_BUILD_ALLOC_WITH_USER_DATA 0)
# 使用各目标的官方调用桥；通用 C 桥不适用 Windows x64 的参数布局。
set(WAMR_BUILD_INVOKE_NATIVE_GENERAL 0)
set(WAMR_DISABLE_HW_BOUND_CHECK 1)
set(WAMR_DISABLE_STACK_HW_BOUND_CHECK 1)

# 实验只接受已核对的源码，不隐式跟随 main 或可变 tag。
set(PROBE_WAMR_COMMIT "25bd7eb63e828e4bd242cc9b38d260b4b31c6605")
execute_process(COMMAND git -C "${WAMR_ROOT_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE actual_commit OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE git_result)
if(NOT git_result EQUAL 0 OR NOT actual_commit STREQUAL PROBE_WAMR_COMMIT)
    message(FATAL_ERROR "Expected WAMR-2.4.5 commit ${PROBE_WAMR_COMMIT}: ${WAMR_ROOT_DIR}")
endif()
execute_process(COMMAND git -C "${WAMR_ROOT_DIR}" status --porcelain --untracked-files=normal
    OUTPUT_VARIABLE source_changes OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE status_result)
if(NOT status_result EQUAL 0 OR NOT source_changes STREQUAL "")
    message(FATAL_ERROR "WAMR checkout must be clean: ${WAMR_ROOT_DIR}")
endif()
