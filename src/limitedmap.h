// Copyright (c) 2012-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_LIMITEDMAP_H
#define BITCOIN_LIMITEDMAP_H

#include <assert.h>
#include <map>

/** STL-like map container that only keeps the N elements with the highest value. */
template <typename K, typename V>
class LimitedMap
{
public:
    typedef K key_type;
    typedef V mapped_type;
    typedef std::pair<const key_type, mapped_type> value_type;
    typedef typename std::map<K, V>::const_iterator const_iterator;
    typedef typename std::map<K, V>::size_type size_type;

protected:
    std::map<K, V> map;
    typedef typename std::map<K, V>::iterator iterator;
    std::multimap<V, iterator> rmap;
    typedef typename std::multimap<V, iterator>::iterator rmap_iterator;
    size_type nMaxSize;

private:
    rmap_iterator find_rmap_key_value_pair(const_iterator map_it) {
        auto [it, it_last] = rmap.equal_range(map_it->second);
        for (; it != it_last; ++it) {
            if (it->second == map_it) {
                return it;
            }
        }
        return rmap.end();
    }

public:
    LimitedMap(size_type nMaxSizeIn = 0) { nMaxSize = nMaxSizeIn; }
    const_iterator begin() const { return map.begin(); }
    const_iterator end() const { return map.end(); }
    size_type size() const { return map.size(); }
    bool empty() const { return map.empty(); }
    const_iterator find(const key_type& k) const { return map.find(k); }
    size_type count(const key_type& k) const { return map.count(k); }

    void insert(const value_type& x) {
        if (nMaxSize == 0) {
            return;
        }

        if (map.size() == nMaxSize) {
            const V& cur_min = rmap.begin()->first;
            if (x.second <  cur_min) {
                return;
            }
            map.erase(rmap.begin()->second);
            rmap.erase(rmap.begin());
        }
        std::pair<iterator, bool> ret = map.insert(x);
        if (ret.second) {
            // in zend, we expect to insert increasing timestamps most of the times, hence the hint.
            rmap.insert(rmap.end(), make_pair(x.second, ret.first));
        }
    }

    size_t erase(const key_type& k) {
        iterator itTarget = map.find(k);
        if (itTarget == map.end())
            return 0;
        rmap_iterator rit = find_rmap_key_value_pair(itTarget);
        assert(rit != rmap.end());
        rmap.erase(rit);
        map.erase(itTarget);
        return 1; // be consistent with std::map return value
    }

    void update(const_iterator itIn, const mapped_type& v) {
        iterator itTarget = map.erase(itIn, itIn); // empty range used to get iterator from const_iterator
        if (itTarget == map.end())
            return;

        rmap_iterator rit = find_rmap_key_value_pair(itIn);
        assert(rit != rmap.end());
        rmap.erase(rit);
        itTarget->second = v;
        rmap.insert(std::make_pair(v, itTarget));
    }

    size_type max_size() const { return nMaxSize; }

    size_type max_size(size_type s) {
        while (map.size() > s) {
            map.erase(rmap.begin()->second);
            rmap.erase(rmap.begin());
        }
        nMaxSize = s;
        return nMaxSize;
    }
};

#endif // BITCOIN_LIMITEDMAP_H
