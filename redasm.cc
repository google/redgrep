// Copyright 2012 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <err.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <string>

#include "llvm-c/Disassembler.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "regexp.h"

int main(int argc, char** argv) {
  const char* argv1 = argv[1];
  if (argv1 == nullptr) {
    errx(1, "regular expression not specified");
  }
  redgrep::Exp exp;
  if (!redgrep::Parse(argv1, &exp)) {
    errx(1, "parse error");
  }
  redgrep::DFA dfa;
  int nstates = redgrep::Compile(exp, &dfa);
  printf("; dfa is %d states\n", nstates);
  redgrep::Fun fun;
  int nbytes = redgrep::Compile(dfa, &fun);
  printf("; fun is %d bytes\n", nbytes);

  std::string triple = fun.engine_->getTargetMachine()->getTargetTriple().str();
  std::string cpu = fun.engine_->getTargetMachine()->getTargetCPU();
  printf("; target is %s (%s)\n", triple.c_str(), cpu.c_str());

  // We need these for the disassembler.
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();
  llvm::InitializeAllDisassemblers();

  LLVMDisasmContextRef disasm = LLVMCreateDisasmCPU(
      triple.c_str(), cpu.c_str(), nullptr, 0, nullptr, nullptr);
  // These are increased and decreased, respectively, as we iterate.
  uint8_t* addr = reinterpret_cast<uint8_t*>(fun.machine_code_addr_);
  uint64_t size = fun.machine_code_size_;
  // These are the bounds.
  uint8_t* base = addr;
  uint8_t* limit = addr + size;
  while (addr < limit) {
    char buf[128];
    size_t len = LLVMDisasmInstruction(disasm, addr, size, 0, buf, sizeof buf);
    if (len == 0) {
      errx(1, "bad machine code at %td (%p)", addr - base, addr);
    }
    printf("%8td%s\n", addr - base, buf);
    addr += len;
    size -= len;
  }
  LLVMDisasmDispose(disasm);
  return 0;
}
