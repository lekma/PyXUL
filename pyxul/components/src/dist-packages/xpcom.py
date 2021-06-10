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


from _xpcom import *


Ci = Components.interfaces
Cr = Components.results


class Interfaces(object):

    interfaces = []

    def QueryInterface(self, iid):
        if iid.equals(Ci.nsISupports):
            return self
        if (
            iid.equals(Ci.nsIClassInfo) and
            (classInfo := getattr(self, "classInfo", None))
        ):
            return classInfo
        for interface in self.interfaces:
            if iid.equals(interface):
                return self
        throwResult(Cr.NS_ERROR_NO_INTERFACE)


class ClassInfo(Interfaces):

    interfaces = [Ci.nsIClassInfo]

    def __init__(self, component):
        self.contractID = component.contractID
        self.classDescription = component.classDescription
        self.classID = component.classID
        self.flags = component.flags
        self.__interfaces__ = component.interfaces
        #self.__interfaces__.sort()

    def getInterfaces(self, count):
        count.value = len(self.__interfaces__)
        return self.__interfaces__

    def getScriptableHelper(self):
        return None


class Factory(Interfaces):

    interfaces = [Ci.nsIFactory]

    def __init__(self, component):
        self.__component__ = component

    def createInstance(self, outer, iid):
        if outer:
            throwResult(Cr.NS_ERROR_NO_AGGREGATION)
        return self.__component__().QueryInterface(iid)

    def lockFactory(self, lock):
        throwResult(Cr.NS_ERROR_NOT_IMPLEMENTED)


class GetFactory(Interfaces):

    def __init__(self, *components):
        self.__components__ = components

    def __call__(self, cid):
        for component in self.__components__:
            if component.classID.equals(cid):
                return Factory(component)
        throwResult(Cr.NS_ERROR_FACTORY_NOT_REGISTERED)


# ------------------------------------------------------------------------------
# Component

class Component(Interfaces):

    __instance__ = None

    classDescription = ""
    flags = 0

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        cls.flags |= Ci.nsIClassInfo.MAIN_THREAD_ONLY
        if not hasattr(cls, "classInfo"):
            cls.classInfo = ClassInfo(cls)

    def __new__(cls):
        if (cls.flags & Ci.nsIClassInfo.SINGLETON):
            if not isinstance(cls.__instance__, cls):
                cls.__instance__ = super().__new__(cls)
            return cls.__instance__
        return super().__new__(cls)

    @property
    def wrappedJSObject(self):
        return self

