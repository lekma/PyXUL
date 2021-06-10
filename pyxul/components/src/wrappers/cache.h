/*
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
*/


#ifndef __pyxul_wrappers_cache_h__
#define __pyxul_wrappers_cache_h__


#include "nsDataHashtable.h"


namespace pyxul::wrappers {


    template<typename K, typename UD>
    class Cache {
        protected:
            typedef nsDataHashtable<nsPtrHashKey<K>, UD *> Table;
            Table mTable;

        public:
            Cache() : mTable(32) {
            }

            virtual ~Cache() {
                mTable.Clear();
            }

            UD *get(K *aKey) {
                return aKey ? mTable.Get(aKey) : nullptr;
            }

            void remove(K *aKey) {
                if (aKey) {
                    mTable.Remove(aKey);
                }
            }

            void put(K *aKey, UD *aData) {
                if (aKey && aData) {
                    mTable.Put(aKey, aData);
                }
            }

            void update(K *aKey, UD *aData) {
                if (aKey && aData && mTable.Contains(aKey)) {
                    mTable.Put(aKey, aData);
                }
            }

            void finalize() {
                K *aKey = nullptr;
                UD *aData = nullptr;

                for (auto iter = mTable.Iter(); !iter.Done(); iter.Next()) {
                    aKey = iter.Key();
                    aData = iter.UserData();
                    iter.Remove();
                    finalizeObject(aKey, aData);
                }
            }

            virtual void finalizeObject(K *aKey, UD *aData) = 0;
    };


} // namespace pyxul::wrappers


#endif // __pyxul_wrappers_cache_h__

