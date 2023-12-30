#pragma once

template <class _Tp>
_Tp& RvToLv(_Tp&& __value)
{
    return __value;
}

struct Range
{
    struct Iterator
    {
        Iterator(int v, int s) : val(v), step(s) {}

        int operator*() const
        {
            return val;
        }

        Iterator& operator++()
        {
            val += step;
            return *this;
        }

        bool operator!=(Iterator const& rhs) const
        {
            return (this->val < rhs.val);
        }

        int val = 0;
        int step = 0;
    };

    Range(int l, int h, int s = 1) : low(l), high(h), step(s) {}
    Iterator begin() const
    {
        return Iterator(low, step);
    }
    Iterator end() const
    {
        return Iterator(high, 1);
    }

    int low = 0, high = 0, step = 0;
};
