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
CFLAGS :=	-funsigned-char -Wall -Wextra \
		-Wno-unused-parameter
CXX :=		g++
CXXFLAGS :=	$(CFLAGS)
CPPFLAGS :=	
LDFLAGS :=	
LDLIBS :=	-lm -lpthread -lstdc++

LLVM_CONFIG ?=	llvm-config
CFLAGS +=	$(shell $(LLVM_CONFIG) --cflags)
CXXFLAGS +=	$(shell $(LLVM_CONFIG) --cxxflags)
LDLIBS +=	$(shell $(LLVM_CONFIG) --ldflags) \
		-Wl,-R$(shell $(LLVM_CONFIG) --libdir) \
		-lLLVM-$(shell $(LLVM_CONFIG) --version)

.PHONY: all
all: regexp_test reddot redasm redgrep

CPPFLAGS +=	-Ithird_party/libutf/include
LIBUTF =	third_party/libutf/runetype/isvalidrune.o \
		third_party/libutf/utf/chartorune.o \
		third_party/libutf/utf/runetochar.o \
		third_party/libutf/utf/runelen.o

CPPFLAGS +=	-Ithird_party/googletest/googletest/include
GOOGLETEST =	third_party/googletest/googletest/src/gtest-all.o \
		third_party/googletest/googletest/src/gtest_main.o

$(GOOGLETEST): CPPFLAGS +=	-Ithird_party/googletest/googletest
$(GOOGLETEST): CXXFLAGS +=	-Wno-missing-field-initializers

parser.tab.cc: parser.yy
	$(YACC.y) $<

parser.tab.o: CXXFLAGS +=	-fexceptions

regexp_test: regexp_test.o $(LIBUTF) parser.tab.o regexp.o $(GOOGLETEST)

reddot: reddot.o $(LIBUTF) parser.tab.o regexp.o

redasm: redasm.o $(LIBUTF) parser.tab.o regexp.o

redgrep: redgrep_main.o $(LIBUTF) parser.tab.o regexp.o redgrep.o

.PHONY: clean
clean:
	$(RM) parser.tab.cc parser.tab.hh location.hh position.hh stack.hh
	$(RM) *.o $(LIBUTF) $(GOOGLETEST)
	$(RM) regexp_test reddot redasm redgrep
