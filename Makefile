##
## This file is part of the sigrok-cli-c++ project.
##
## Copyright (C) 2014 Martin Ling <martin-sigrok@earth.li>
##
## This program is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <http://www.gnu.org/licenses/>.

CXXFLAGS=$(shell pkg-config --cflags --libs libsigrokflow libsigrokcxx libsigrokdecode)

sigrok-cli-c++: sigrok-cli.cpp cpp-optparse/OptionParser.cpp
	g++ -std=c++11 $^ $(CXXFLAGS) -o $@

clean:
	rm sigrok-cli-c++
