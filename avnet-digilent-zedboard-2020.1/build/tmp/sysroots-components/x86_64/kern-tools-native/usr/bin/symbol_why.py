#!/usr/bin/env python
# SPDX-License-Identifier: GPL-2.0-only
#

# Kconfig symbol analsysis
#
# Copyright (C) 2018 Bruce Ashfield
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

## example:
##     KERNELVERSION=4.7-rc5 SRCARCH=x86 ARCH=x86 ./Kconfiglib/examples/symbol_why.py

import sys
import os

# Kconfiglib should be installed into an existing python library
# location OR a path to where the library is should be set via something
# like: PYTHONPATH="./Kconfiglib:$PYTHONPATH".
#
# But if neither of those are true, let's try and find it ourselves
#
pathname = os.path.dirname(sys.argv[0])
try:
    import kconfiglib
except ImportError:
    sys.path.append( pathname + '/Kconfiglib')
    try:
        import kconfiglib
    except ImportError:
        raise ImportError('Could not import kconfiglib, make sure it is properly installed')

import argparse
import re
import os

dotconfig = ''
ksrc = ''
verbose = ''
show_summary = False
show_vars = False
show_selected = False
show_prompt = False
show_conditions = False
show_value = False

parser = argparse.ArgumentParser(
                formatter_class=argparse.ArgumentDefaultsHelpFormatter,
                description="Kconfig symbol determination")

parser.add_argument("-c", "--dotconfig",
                    help="path to the .config to load")
parser.add_argument("-s", "--ksrc",
                    help="path to the kernel source")
parser.add_argument("-v", action='store_true',
                    help="verbose")
parser.add_argument("--summary", action='store_true',
                    help="Show variable summary")
parser.add_argument("--prompt", action='store_true',
                    help="Show variable prompt")
parser.add_argument("--conditions", action='store_true',
                    help="Show config option dependencies" )
parser.add_argument("--vars", action='store_true',
                    help="Show the dependent variables" )
parser.add_argument("--value", action='store_true',
                    help="Show the config value" )
parser.add_argument("config", help="configuration option to check")
parser.add_argument('args', help="<path to .config> <path to kernel source tree>", nargs=argparse.REMAINDER)

args, unknownargs = parser.parse_known_args()

# pull these out of args, since we want to test the variables .. and they
# can bet set by more than the command line
if args.dotconfig:
    dotconfig=args.dotconfig
if args.ksrc:
    ksrc=args.ksrc
if args.v:
    verbose=True
if args.config:
    option=args.config
if args.summary:
    show_summary=args.summary
if args.prompt:
    show_prompt=args.prompt
if args.conditions:
    show_conditions=args.conditions
if args.vars:
    show_vars=args.vars
if args.value:
    show_value=args.value

# a little extra processing, since argparse will stop at the first non
# dashed option. We take whatever is left over, check to see if all our
# options are defined .. and if they aren't we use these ones.
for opt in args.args:
    if opt == '-h' or opt == "--help":
        parser.print_help()
        sys.exit()
    elif opt == '-v':
        verbose=1
    elif opt == '--summary':
        show_summary=True
    elif opt == '--conditions':
        show_conditions=True
    elif opt == '--prompt':
        show_prompt=True
    elif opt == '--vars':
        show_vars=True
    elif opt == '--value':
        show_value=True
    elif re.match( "--dotconfig=*", opt):
        temp, dotconfig = opt.split('=', 2)
    elif re.match( "--ksrc=*", opt):
        temp, ksrc = opt.split('=', 2)
    else:
        if re.match( ".*\.config", opt ):
            dotconfig=opt
        elif not ksrc:
            ksrc=opt

if not os.path.exists( dotconfig ):
    print( "ERROR: .config '%s' does not exist" % dotconfig )
    sys.exit(1)

# There are three required environment variables:
#  - KERNELVERSION
#  - SRCARCH
#  - ARCH
#  - srctree
#  - CC
#
# If SRCARCH isn't set, but ARCH is, we simply make SRCARCH=ARCH, but
# other missing variables are an error
#
if not os.getenv("KERNELVERSION"):
    hconfig = open( dotconfig )
    for line in hconfig:
        line = line.rstrip()
        x = re.match( "^# .*Linux/\w*\s*([0-9]*\.[0-9]*\.[0-9]*).*Kernel Configuration", line )
        if x:
            os.environ["KERNELVERSION"] = x.group(1)
            if verbose:
                print( "[INFO]: kernel version %s found in .config, if this is incorrect, set KERNELVERSION in the environement" % x.group(1) )

    if not os.getenv("KERNELVERSION"):
        os.environ["KERNELVERSION"] = "4.7"
        if verbose:
            print( "[INFO]: default kernel version 4.7 used, if this is incorrect, set KERNELVERSION in the environement" )

if not os.getenv("SRCARCH"):
    if os.getenv("ARCH"):
        os.environ["SRCARCH"] = os.getenv("ARCH")
    else:
        print( "ERROR: source arch must be set (via SRCARCH environment variable" )
        sys.exit(1)

if not os.getenv("ARCH"):
    print( "ERROR: arch must be set (via ARCH environment variable" )
    sys.exit(1)

if not ksrc:
    ksrc = "."

kconf = ksrc + "/Kconfig"
if not os.path.exists( kconf ):
    print( "ERROR: kernel source directory '%s' does not contain a top level Kconfig file" % ksrc )
    sys.exit(1)

if verbose:
    print( "[INFO]: dotconfig: " + dotconfig)
    print( "[INFO]: ksrc: " + ksrc )
    print( "[INFO]: option: " + option )
    print( "[INFO]: kernel ver: " + format(os.getenv("KERNELVERSION")) )
    print( "[INFO]: src arch: " + os.getenv("SRCARCH") )
    print( "[INFO]: arch: " + os.getenv("ARCH") )

def _is_num(name):
    # Heuristic to see if a symbol name looks like a number, for nicer output
    # when printing expressions. Things like 16 are actually symbol names, only
    # they get their name as their value when the symbol is undefined.

    try:
        int(name)
    except ValueError:
        if not name.startswith(("0x", "0X")):
            return False

        try:
            int(name, 16)
        except ValueError:
            return False

    return True

def _name_and_val_str(sc):
    # Custom symbol printer that shows the symbol value after the symbol, used
    # for the information display

    # Show the values of non-constant (non-quoted) symbols that don't look like
    # numbers. Things like 123 are actually symbol references, and only work as
    # expected due to undefined symbols getting their name as their value.
    # Showing the symbol value for those isn't helpful though.
    if isinstance(sc, kconfiglib.Symbol) and \
       not sc.is_constant and \
       not _is_num(sc.name):

        if not sc.nodes:
            # Undefined symbol reference
            return "{}(undefined/n)".format(sc.name)

        return '{}(={})'.format(sc.name, sc.str_value)

    # For other symbols, use the standard format
    return standard_sc_expr_str(sc)

def _expr_str(expr):
    # Custom expression printer that shows symbol values
    return kconfiglib.expr_str(expr, _name_and_val_str)

def _split_expr_info(expr, indent):
    # Returns a string with 'expr' split into its top-level && or || operands,
    # with one operand per line, together with the operand's value. This is
    # usually enough to get something readable for long expressions. A fancier
    # recursive thingy would be possible too.
    #
    # indent:
    #   Number of leading spaces to add before the split expression.

    if len(kconfiglib.split_expr(expr, kconfiglib.AND)) > 1:
        split_op = kconfiglib.AND
        op_str = "&&"
    else:
        split_op = kconfiglib.OR
        op_str = "||"

    s = ""
    for i, term in enumerate(kconfiglib.split_expr(expr, split_op)):
        s += "{}{} {}".format(" "*indent,
                              "  " if i == 0 else op_str,
                              _expr_str(term))

        # Don't bother showing the value hint if the expression is just a
        # single symbol. _expr_str() already shows its value.
        if isinstance(term, tuple):
            s += "  (={})".format(kconfiglib.TRI_TO_STR[kconfiglib.expr_value(term)])

        s += "\n"

    return s

def _direct_dep_info(sc):
    # Returns a string describing the direct dependencies of 'sc' (Symbol or
    # Choice). The direct dependencies are the OR of the dependencies from each
    # definition location. The dependencies at each definition location come
    # from 'depends on' and dependencies inherited from parent items.

    return 'Direct dependencies (={}):\n{}' \
        .format(kconfiglib.TRI_TO_STR[kconfiglib.expr_value(sc.direct_dep)], _split_expr_info(sc.direct_dep, 2))

def referencing_nodes(node, sym):
    # Returns a list of all menu nodes that reference 'sym' in any of their
    # properties or property conditions

    res = []

    while node:
        if sym in node.referenced:
            res.append(node)

        if node.list:
            res.extend(referencing_nodes(node.list, sym))

        node = node.next

    return res

# Create a Config object representing a Kconfig configuration. (Any number of
# these can be created -- the library has no global state.
show_errors = False
if verbose:
    show_errors = True

conf = kconfiglib.Kconfig( kconf, show_errors, show_errors )

# Load values from a .config file.
conf.load_config( dotconfig )

if option not in conf.syms:
    print("No symbol {} exists in the configuration".format(option))
    sys.exit(0)

opt = conf.syms[option]
nodes = referencing_nodes(conf.top_node, conf.syms[option])
if not nodes:
    print("No reference to {} found".format(option))
    sys.exit(0)

if show_summary:
    sym=conf.syms[option]
    print(sym)
    print("  Value: " + sym.str_value)
    print("  Visibility: " + kconfiglib.TRI_TO_STR[sym.visibility])
    print("  Currently assignable values: " +
          ", ".join([kconfiglib.TRI_TO_STR[v] for v in sym.assignable]))

    for node in sym.nodes:
        print("  defined at {}:{}".format(node.filename, node.linenr))

if show_vars:
    print( "" )
    print( "Variables that depend on '%s':" % option )

    for sym in conf.syms:
        if opt in sym.get_referenced_symbols():
            print("    " + sym.get_name())

if show_prompt:
    print( "Prompt for '%s': %s" % (option,opt.get_prompts()) )

refs = opt.referenced
deps = opt.direct_dep
imps = opt.implies
selected = opt.selects
selected_names = []
for s in selected:
    selected_names.append(s[0].name)

#print(opt.direct_dep)
#print(_direct_dep_info(opt))
# for s in selected:
#     s is the tuple
#     print(s)
#     print(s[0].name)

if show_selected:
     for sel in selected:
         print(s[0].name)

if show_conditions:
    depends_string=""
    dep_string = _direct_dep_info(opt)
    dep_string = dep_string.replace('\n', ' ').replace('\r', '')
    dep_string = ' '.join(dep_string.split())
    dep_string = dep_string.replace(':', ':\n       ')
    print("  Config '%s' has the following %s" % (option, dep_string))

    for s in refs:
        if not s.name in selected_names:
            depends_string += " " + s.name + " [" + s.str_value + "]"
    if verbose:
        print( "  Dependency values are: " )
        print( "    %s" % depends_string )

if show_value:
    print( "Config '%s' has value: %s" % (option, opt.get_value()))
