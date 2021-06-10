# -*- coding: utf-8 -*-

# Python for XUL
# copyright © 2021 Malek Hadj-Ali
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 3
# as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.


import sysconfig
import os
import sys
import argparse


get_var = sysconfig.get_config_var

python_name = "".join(("python", get_var("VERSION")))
if os.name == "nt":
    python_libname = python_name
else:
    python_libname = "".join((python_name, sys.abiflags))


exclude_flags = set(["-Wstrict-prototypes"])

def strip_flags(flags):
    result = []
    for flag in flags:
        if flag not in exclude_flags:
            result.append(flag)
    return result


def __libname__():
    """print the Python library name and exit."""
    return python_libname


def __includes__():
    """print the Python includes required to build and exit."""
    includes = set((sysconfig.get_path("include"), sysconfig.get_path("platinclude")))
    return " ".join("".join(("-I", path)) for path in includes)


def __cflags__():
    """print the Python cflags required to build and exit."""
    cflags_vars = ("BASECFLAGS", "CONFIGURE_CFLAGS", "OPT")
    cflags = " ".join((get_var(var) for var in cflags_vars)).split()
    return " ".join(strip_flags(cflags))


def __ldflags__():
    """print the Python ldflags required to build and exit."""
    ldflags = []
    if os.name == "nt":
        lib_path = os.path.join(os.path.normpath(sys.exec_prefix), "libs")
        ldflags.append(os.path.join(lib_path, ".".join((python_name, "lib"))))
    else:
        if not get_var("Py_ENABLE_SHARED"):
            ldflags.append("".join(("-L", get_var("LIBPL"))))
        ldflags.append("".join(("-L", get_var("LIBDIR"))))
        ldflags.extend(" ".join((get_var("LIBS"), get_var("SYSLIBS"))).split())
        ldflags.append("".join(("-l", python_libname)))
        if not get_var("PYTHONFRAMEWORK"):
            ldflags.extend(get_var("LINKFORSHARED").split())
    return " ".join(ldflags)


arguments = {
    "--libname": __libname__,
    "--includes": __includes__,
    "--cflags": __cflags__,
    "--ldflags": __ldflags__,
}


class PrintAndExitAction(argparse.Action):

    def __init__(
        self, option_strings, dest=argparse.SUPPRESS, help=None, value=None
    ):
        help = help or (value.__doc__ if callable(value) else None)
        super().__init__(option_strings, dest, nargs=0, help=help)
        self.value = value

    def __call__(self, parser, namespace, values, option_string=None):
        print(self.value() if callable(self.value) else self.value)
        parser.exit()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    for key, value in arguments.items():
        group.add_argument(key, action=PrintAndExitAction, value=value)
    parser.parse_args()

