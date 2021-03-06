#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

APXS?=apxs
PLUADBD?=0

none:
	@echo "Please do 'make VERSION' where VERSION is one of these:"
	@echo "    5.1 5.2 luajit"
	@echo " "
	@echo "To add support for mod_dbd, run the following command before compiling:"
	@echo "    export PLUADBD=1"

luajit: mod_plua.c
	$(APXS) -D_WITH_MOD_DBD=${PLUADBD} -I/usr/include/luajit-2.0 -lluajit-5.1 -c $?
	
5.1: mod_plua.c
	$(APXS) -D_WITH_MOD_DBD=${PLUADBD} -I/usr/include/lua5.1 -llua5.1 -lm -c $?

5.2: mod_plua.c
	$(APXS) -D_WITH_MOD_DBD=${PLUADBD} -llua -lm -c $?
	
install:
	$(APXS) -a -i -n plua mod_plua.la

clean:
	rm -rf *.lo *.o *.slo *.la .libs
	
