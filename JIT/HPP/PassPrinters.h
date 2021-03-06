//===- PassPrinters.h - Utilities to print analysis info for passes -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Utilities to print analysis info for various kinds of passes.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_TOOLS_OPT_PASSPRINTERS_H
#define LLVM_TOOLS_OPT_PASSPRINTERS_H

namespace llvm {

  class BasicBlockPass;
  class CallGraphSCCPass;
  class FunctionPass;
  class ModulePass;
  class LoopPass;
  class PassInfo;
  class RegionPass;
  class raw_ostream;

  FunctionPass *createFunctionPassPrinter(const PassInfo *passInfo, raw_ostream &out,
					  bool Quiet);

  CallGraphSCCPass *createCallGraphPassPrinter(const PassInfo *passInfo,
					       raw_ostream &out, bool Quiet);

  ModulePass *createModulePassPrinter(const PassInfo *passInfo, raw_ostream &out,
				      bool Quiet);

  LoopPass *createLoopPassPrinter(const PassInfo *passInfo, raw_ostream &out,
				  bool Quiet);

  RegionPass *createRegionPassPrinter(const PassInfo *passInfo, raw_ostream &out,
				      bool Quiet);

  BasicBlockPass *createBasicBlockPassPrinter(const PassInfo *passInfo,
					      raw_ostream &out, bool Quiet);
}

#endif // LLVM_TOOLS_OPT_PASSPRINTERS_H
