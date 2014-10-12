# Copyright 2012 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

YACC :=		bison
CC :=		gcc
CFLAGS :=	-Wall -Wextra -funsigned-char
CXX :=		g++
CXXFLAGS :=	-std=c++0x $(CFLAGS)
CPPFLAGS :=	-I.
LDFLAGS :=	
LDLIBS :=	-lpthread -lstdc++

LLVM_CONFIG ?=	llvm-config
CFLAGS +=	$(shell $(LLVM_CONFIG) --cflags)
CXXFLAGS +=	$(shell $(LLVM_CONFIG) --cxxflags)
LDLIBS +=	$(shell $(LLVM_CONFIG) --ldflags) \
		-R$(shell $(LLVM_CONFIG) --libdir) \
		-lLLVM-$(shell $(LLVM_CONFIG) --version)

.PHONY: all
all: utf gtest regexp_test reddot redasm redgrep

utf:
	wget -m -np -q https://go.googlecode.com/hg/src/lib9/utf/
	ln -s go.googlecode.com/hg/src/lib9/utf/ .

gtest:
	wget -m -np -q https://googletest.googlecode.com/svn/trunk/
	python2 googletest.googlecode.com/svn/trunk/scripts/fuse_gtest_files.py .
	ln -s ../googletest.googlecode.com/svn/trunk/src/gtest_main.cc gtest/

UTF =	utf/rune.o
GTEST =	gtest/gtest-all.o gtest/gtest_main.o

parser.tab.cc: parser.yy
	$(YACC.y) $<

parser.tab.o: parser.tab.cc
	$(COMPILE.cc) $(OUTPUT_OPTION) $<	-fexceptions

regexp_test: regexp_test.o $(UTF) parser.tab.o regexp.o $(GTEST)

reddot: reddot.o $(UTF) parser.tab.o regexp.o

redasm: redasm.o $(UTF) parser.tab.o regexp.o

redgrep: redgrep_main.o $(UTF) parser.tab.o regexp.o redgrep.o

.PHONY: clean
clean:
	$(RM) parser.tab.cc parser.tab.hh location.hh position.hh stack.hh
	$(RM) *.o utf/*.o gtest/*.o
	$(RM) regexp_test reddot redasm redgrep
