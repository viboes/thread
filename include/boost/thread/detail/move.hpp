// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
// (C) Copyright 2007 Anthony Williams

#ifndef BOOST_THREAD_MOVE_HPP
#define BOOST_THREAD_MOVE_HPP

#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_convertible.hpp>

namespace boost
{
    namespace detail
    {
        template<typename T>
        struct thread_move_t
        {
            T& t;
            explicit thread_move_t(T& t_):
                t(t_)
            {}

            T& operator*() const
            {
                return t;
            }

            T* operator->() const
            {
                return &t;
            }
        private:
            void operator=(thread_move_t&);
        };
    }

    template<typename T>
    typename enable_if<boost::is_convertible<T&,detail::thread_move_t<T> >, detail::thread_move_t<T> >::type move(T& t)
    {
        return t;
    }
    
    template<typename T>
    detail::thread_move_t<T> move(detail::thread_move_t<T> t)
    {
        return t;
    }
    
}


#endif
