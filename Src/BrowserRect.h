/* @@@LICENSE
*
*      Copyright (c) 2012 Hewlett-Packard Development Company, L.P.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
LICENSE@@@ */

#ifndef BROWSERRECT_H
#define BROWSERRECT_H

#include <string.h>

#include <algorithm>

template <typename T>
class _TemplateRect {
public:
    _TemplateRect(): m_x(0), m_y(0), m_w(0), m_h(0){};
    _TemplateRect(T x, T y, T w, T h): m_x(x), m_y(y), m_w(w), m_h(h){};
    _TemplateRect(const _TemplateRect<T>& r): m_x(r.m_x), m_y(r.m_y), m_w(r.m_w), m_h(r.m_h){};
    _TemplateRect& operator=(const _TemplateRect<T>& r){
        if (this != &r) {
            m_x = r.m_x;
            m_y = r.m_y;
            m_w = r.m_w;
            m_h = r.m_h;
        }

        return *this;
    };
    bool operator==(const _TemplateRect<T>& r) const
    {
        return ((m_x == r.m_x) &&
                (m_y == r.m_y) &&
                (m_w == r.m_w) &&
                (m_h == r.m_h));
    };
    bool empty() const { return (m_w <= 0) || (m_h <= 0); }

    T x() const { return m_x; }
    T y() const { return m_y; }
    T w() const { return m_w; }
    T h() const { return m_h; }
    T r() const { return m_x+m_w; }
    T b() const { return m_y+m_h; }
	
    void setX(T x){m_x=x;}
    void setY(T y){m_y=y;}
    void setWidth(T w){m_w=w;}
    void setHeight(T h){m_h=h;}
	void set( T x, T y, T r, T b ) { m_x=x; m_y=y; m_w=r-x; m_h=b-y; }
    void moveTo(T x, T y) { m_x = x; m_y = y; }
    void moveBy(T dx, T dy) { m_x += dx; m_y += dy; }

    bool intersects(const _TemplateRect<T>& rect) const
    {
        if (empty() || rect.empty())
            return false;

        int l = std::max(m_x, rect.m_x);
        int b = std::min(m_y + m_h, rect.m_y + rect.m_h);
        int t = std::max(m_y, rect.m_y);
        int r = std::min(m_x + m_w, rect.m_x + rect.m_w);

        return (l < r) && (t < b);
    };

    void intersect(const _TemplateRect<T>& rect)
    {
        T l = std::max(m_x, rect.m_x);
        T b = std::min(m_y + m_h, rect.m_y + rect.m_h);
        T t = std::max(m_y, rect.m_y);
        T r = std::min(m_x + m_w, rect.m_x + rect.m_w);

        if ((l >= r) || (t >= b)) {
            m_x = 0;
            m_y = 0;
            m_w = 0;
            m_h = 0;
        }
        else {
            m_x = l;
            m_y = t;
            m_w = r - l;
            m_h = b - t;
        }
    };

    void unite(const _TemplateRect<T>& rect)
    {
        if (rect.empty()) {
            return;
        }

        if (empty()) {
            *this = rect;
            return;
        }
        
        T l = std::min(m_x, rect.m_x);
        T t = std::min(m_y, rect.m_y);
        T r = std::max(m_x + m_w, rect.m_x + rect.m_w);
        T b = std::max(m_y + m_h, rect.m_y + rect.m_h);

        m_x = l;
        m_y = t;
        m_w = r - l;
        m_h = b - t;
    }

    bool overlaps(const _TemplateRect<T>& r) const
    {

    	if (empty() || r.empty())
    		return false;
    	
    	return ((m_x <= r.m_x) &&
    			(m_x+m_w >= r.m_x+r.m_w) &&
    			(m_y <= r.m_y) &&
    			(m_y+m_h >= r.m_y+r.m_h));

    };

    int subtract(const _TemplateRect<T>& r, _TemplateRect<T>* result) const
    {

    	int index = 0;
    	// Case 0: this rect is empty
    	if (empty())
    		return index;

    	// Case 1: other rect is empty
    	if (r.empty()) {
    		memcpy(result+(index++), this, sizeof(_TemplateRect<T>));
    		return index;
    	}

    	// Case 2: rects don't intersect
    	if (!intersects(r)) {			
    		memcpy(result+(index++), this, sizeof(_TemplateRect<T>));
    		return index;
    	}

    	// Case 3: other rect overlaps this. result is an empty rectangle
    	if (r.overlaps(*this)) {
    		return index;
    	}

    	// Case 4: general intersection: 4 possible strips (top, bottom, left, right)
    	// optimize for wider strips: i.e. make top and bottom as wide as possible

    	// Top Strip?
    	if (y() < r.y()) {

    		_TemplateRect<T>& a = *(result+index);
    		a.m_y = m_y;
    		a.m_h = r.m_y - m_y;
    		a.m_x = m_x;
    		a.m_w = m_w;

    		if (!a.empty())
    			index++;
    	}

    	// Bottom Strip?
    	if (b() > r.b()) {

    		_TemplateRect<T>& a = *(result+index);
    		a.m_y = r.m_y + r.m_h;
    		a.m_h = m_y + m_h - a.m_y;
    		a.m_x = m_x;
    		a.m_w = m_w;

    		if (!a.empty())
    			index++;
    	}

    	// Left Strip?
    	if (x() < r.x()) {

    		_TemplateRect<T>& a = *(result+index);
    		a.m_x = m_x;
    		a.m_w = r.m_x - m_x;
    		a.m_y = std::max(m_y, r.m_y);
    		a.m_h = std::min(b(), r.b()) - a.m_y;

    		if (!a.empty())
    			index++;
    	}

    	// Right Strip?
    	if (this->r() > r.r()) {

    		_TemplateRect<T>& a = *(result+index);
    		a.m_x = r.r();
    		a.m_w = this->r() - a.m_x;
    		a.m_y = std::max(y(), r.y());
    		a.m_h = std::min(b(), r.b()) - a.m_y;

    		if (!a.empty())
    			index++;
    	}

    	return index;
    }

	
	void scale( double factor )
	{
		if( 1.0f == factor ) 
			return;
		m_x = (T)( m_x * factor );
		m_y = (T)( m_y * factor );
		m_w = (T)( m_w * factor );
		m_h = (T)( m_h * factor );
	}
    
private:

    T m_x;
    T m_y;
    T m_w;
    T m_h;
};

typedef _TemplateRect<int> BrowserRect;

typedef _TemplateRect<double> BrowserDoubleRect;

extern BrowserRect doubleToIntRoundDown(const BrowserDoubleRect & rect);

extern BrowserRect doubleToIntRoundUp(const BrowserDoubleRect & rect);

#endif /* BROWSERRECT_H */
