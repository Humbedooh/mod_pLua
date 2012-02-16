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
WITHDBD?=

none:
	@echo "Please do 'make VERSION' where VERSION is one of these:"
	@echo "   5.1 5.2 luajit luajit_debian"

luajit: mod_plua.c
	$(APXS) -D_WITH_DBD=${WITHDBD} -I/usr/include/luajit-2.0 -lluajit-5.1 -c $?
	
5.1: mod_plua.c
	$(APXS) -I/usr/include/lua5.1 -llua5.1 -c $?

5.2: mod_plua.c
	$(APXS) -llua -c $?
	
install:
	$(APXS) -a -i -n plua mod_plua.la

clean:
	rm -f *.lo *.so
	
