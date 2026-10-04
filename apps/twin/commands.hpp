/**
 * @file commands.hpp
 * @brief Subcommands of the `twin` command-line tool.
 *
 * Each subcommand is a function `int run_xxx(int argc, char** argv)` that
 * receives the arguments after the subcommand name and returns a
 * twin::cli::ExitCode.
 */
#pragma once

namespace twin::cli {

/// `twin compile <model.xml> --id ID [--version V] [--interp F] [--ticks-per-unit R] [--out DIR]`
int run_compile(int argc, char** argv);
/// `twin align --pt PT.xml --dt DT.xml --ontology O.ont --pt-interp P.interp --dt-interp D.interp [--out F]`
int run_align(int argc, char** argv);
/// `twin package build|verify|inspect ...`
int run_package(int argc, char** argv);
/// `twin ledger verify|show|anchor ...`
int run_ledger(int argc, char** argv);
/// `twin replay --package DIR --ledger FILE`
int run_replay(int argc, char** argv);

}  // namespace twin::cli
