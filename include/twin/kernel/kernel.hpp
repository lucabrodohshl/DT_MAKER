/**
 * @file kernel.hpp
 * @brief Umbrella header of the trusted semantic kernel (twin::kernel).
 * @ingroup kernel
 *
 * @defgroup kernel Semantic kernel (trusted)
 * @brief K_sem: the minimal executable semantics of the Twin IR.
 *
 * The kernel is the single semantic authority of the Digital Twin. Its library
 * target (twin_kernel) links only twin::ir_model and twin::core: it performs no
 * I/O, no networking, no persistence, no logging and no serialisation. Those
 * concerns belong to the production shell (twin::runtime), which may call the
 * kernel but can never change semantic state except through it.
 */
#pragma once

#include "twin/kernel/configuration.hpp"
#include "twin/kernel/explore.hpp"
#include "twin/kernel/instance.hpp"
#include "twin/kernel/model.hpp"
#include "twin/kernel/semantics.hpp"
#include "twin/kernel/state_set.hpp"
