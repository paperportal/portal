#pragma once

#include <stddef.h>
#include <stdint.h>

#include "wasm_export.h"

static inline wasm_module_inst_t wasm_api_get_module_inst(wasm_exec_env_t exec_env)
{
    return wasm_runtime_get_module_inst(exec_env);
}

static inline bool wasm_api_validate_app_addr(wasm_exec_env_t exec_env, uint32_t offset, uint32_t size)
{
    wasm_module_inst_t inst = wasm_api_get_module_inst(exec_env);
    return inst && wasm_runtime_validate_app_addr(inst, (uint64_t)offset, (uint64_t)size);
}

static inline bool wasm_api_validate_app_str_addr(wasm_exec_env_t exec_env, uint32_t offset)
{
    wasm_module_inst_t inst = wasm_api_get_module_inst(exec_env);
    return inst && wasm_runtime_validate_app_str_addr(inst, (uint64_t)offset);
}

static inline void *wasm_api_addr_app_to_native(wasm_exec_env_t exec_env, uint32_t offset)
{
    wasm_module_inst_t inst = wasm_api_get_module_inst(exec_env);
    if (!inst) {
        return NULL;
    }
    return wasm_runtime_addr_app_to_native(inst, (uint64_t)offset);
}
