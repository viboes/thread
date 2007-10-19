#ifndef BOOST_THREAD_CONDITION_VARIABLE_WIN32_HPP
#define BOOST_THREAD_CONDITION_VARIABLE_WIN32_HPP
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
// (C) Copyright 2007 Anthony Williams

#include <boost/thread/mutex.hpp>
#include "thread_primitives.hpp"
#include <limits.h>
#include <boost/assert.hpp>
#include <algorithm>
#include <boost/thread/thread.hpp>
#include <boost/thread/thread_time.hpp>
#include "interlocked_read.hpp"

namespace boost
{
    namespace detail
    {
        class basic_condition_variable
        {
            boost::mutex internal_mutex;
            long total_count;
            unsigned active_generation_count;

            struct list_entry
            {
                detail::win32::handle semaphore;
                long count;
                bool notified;
            };

            BOOST_STATIC_CONSTANT(unsigned,generation_count=3);

            list_entry generations[generation_count];
            detail::win32::handle wake_sem;

            static bool no_waiters(list_entry const& entry)
            {
                return entry.count==0;
            }

            void shift_generations_down()
            {
                list_entry* const last_active_entry=std::remove_if(generations,generations+generation_count,no_waiters);
                if(last_active_entry==generations+generation_count)
                {
                    broadcast_entry(generations[generation_count-1],false);
                }
                else
                {
                    active_generation_count=(last_active_entry-generations)+1;
                }
            
                std::copy_backward(generations,generations+active_generation_count-1,generations+active_generation_count);
                generations[0]=list_entry();
            }

            void broadcast_entry(list_entry& entry,bool wake)
            {
                long const count_to_wake=entry.count;
                detail::interlocked_write_release(&total_count,total_count-count_to_wake);
                if(wake)
                {
                    detail::win32::ReleaseSemaphore(wake_sem,count_to_wake,0);
                }
                detail::win32::ReleaseSemaphore(entry.semaphore,count_to_wake,0);
                entry.count=0;
                dispose_entry(entry);
            }
        

            void dispose_entry(list_entry& entry)
            {
                if(entry.semaphore)
                {
                    unsigned long const close_result=detail::win32::CloseHandle(entry.semaphore);
                    BOOST_ASSERT(close_result);
                    entry.semaphore=0;
                }
                entry.notified=false;
            }

            template<typename lock_type>
            struct relocker
            {
                lock_type& lock;
                bool unlocked;
                
                relocker(lock_type& lock_):
                    lock(lock_),unlocked(false)
                {}
                void unlock()
                {
                    lock.unlock();
                    unlocked=true;
                }
                ~relocker()
                {
                    if(unlocked)
                    {
                        lock.lock();
                    }
                    
                }
            };
            

        protected:
            template<typename lock_type>
            bool do_wait(lock_type& lock,::boost::system_time const& wait_until)
            {
                detail::win32::handle_manager local_wake_sem;
                detail::win32::handle_manager sem;
                bool first_loop=true;
                bool woken=false;

                relocker<lock_type> locker(lock);
            
                while(!woken)
                {
                    {
                        boost::mutex::scoped_lock internal_lock(internal_mutex);
                        detail::interlocked_write_release(&total_count,total_count+1);
                        if(first_loop)
                        {
                            locker.unlock();
                            if(!wake_sem)
                            {
                                wake_sem=detail::win32::create_anonymous_semaphore(0,LONG_MAX);
                                BOOST_ASSERT(wake_sem);
                            }
                            local_wake_sem=detail::win32::duplicate_handle(wake_sem);
                        
                            if(generations[0].notified)
                            {
                                shift_generations_down();
                            }
                            else if(!active_generation_count)
                            {
                                active_generation_count=1;
                            }
                        
                            first_loop=false;
                        }
                        if(!generations[0].semaphore)
                        {
                            generations[0].semaphore=detail::win32::create_anonymous_semaphore(0,LONG_MAX);
                            BOOST_ASSERT(generations[0].semaphore);
                        }
                        ++generations[0].count;
                        sem=detail::win32::duplicate_handle(generations[0].semaphore);
                    }
                    unsigned long const wait_result=detail::win32::WaitForSingleObject(sem,::boost::detail::get_milliseconds_until(wait_until));

                    if(wait_result==detail::win32::timeout)
                    {
                        break;
                    }
                    BOOST_ASSERT(!wait_result);
                
                    unsigned long const woken_result=detail::win32::WaitForSingleObject(local_wake_sem,0);
                    BOOST_ASSERT(woken_result==detail::win32::timeout || woken_result==0);

                    woken=(woken_result==0);
                }
                return woken;
            }
        
            basic_condition_variable(const basic_condition_variable& other);
            basic_condition_variable& operator=(const basic_condition_variable& other);
        public:
            basic_condition_variable():
                total_count(0),active_generation_count(0),wake_sem(0)
            {
                for(unsigned i=0;i<generation_count;++i)
                {
                    generations[i]=list_entry();
                }
            }
            
            ~basic_condition_variable()
            {
                for(unsigned i=0;i<generation_count;++i)
                {
                    dispose_entry(generations[i]);
                }
                detail::win32::CloseHandle(wake_sem);
            }

        
            void notify_one()
            {
                if(detail::interlocked_read_acquire(&total_count))
                {
                    boost::mutex::scoped_lock internal_lock(internal_mutex);
                    detail::win32::ReleaseSemaphore(wake_sem,1,0);
                    for(unsigned generation=active_generation_count;generation!=0;--generation)
                    {
                        list_entry& entry=generations[generation-1];
                        if(entry.count)
                        {
                            detail::interlocked_write_release(&total_count,total_count-1);
                            entry.notified=true;
                            detail::win32::ReleaseSemaphore(entry.semaphore,1,0);
                            if(!--entry.count)
                            {
                                dispose_entry(entry);
                                if(generation==active_generation_count)
                                {
                                    --active_generation_count;
                                }
                            }
                        }
                    }
                }
            }
        
            void notify_all()
            {
                if(detail::interlocked_read_acquire(&total_count))
                {
                    boost::mutex::scoped_lock internal_lock(internal_mutex);
                    for(unsigned generation=active_generation_count;generation!=0;--generation)
                    {
                        list_entry& entry=generations[generation-1];
                        if(entry.count)
                        {
                            broadcast_entry(entry,true);
                        }
                    }
                    active_generation_count=0;
                }
            }
        
        };
    }

    class condition_variable:
        public detail::basic_condition_variable
    {
    public:
        void wait(unique_lock<mutex>& m)
        {
            do_wait(m,::boost::detail::get_system_time_sentinel());
        }

        template<typename predicate_type>
        void wait(unique_lock<mutex>& m,predicate_type pred)
        {
            while(!pred()) wait(m);
        }
        

        bool timed_wait(unique_lock<mutex>& m,boost::system_time const& wait_until)
        {
            return do_wait(m,wait_until);
        }

        template<typename predicate_type>
        bool timed_wait(unique_lock<mutex>& m,boost::system_time const& wait_until,predicate_type pred)
        {
            while (!pred())
            {
                if(!timed_wait(m, wait_until))
                    return false;
            }
            return true;
        }
    };
    
    class condition_variable_any:
        public detail::basic_condition_variable
    {
    public:
        template<typename lock_type>
        void wait(lock_type& m)
        {
            do_wait(m,::boost::detail::get_system_time_sentinel());
        }

        template<typename lock_type,typename predicate_type>
        void wait(lock_type& m,predicate_type pred)
        {
            while(!pred()) wait(m);
        }
        
        template<typename lock_type>
        bool timed_wait(lock_type& m,boost::system_time const& wait_until)
        {
            return do_wait(m,wait_until);
        }

        template<typename lock_type,typename predicate_type>
        bool timed_wait(lock_type& m,boost::system_time const& wait_until,predicate_type pred)
        {
            while (!pred())
            {
                if(!timed_wait(m, wait_until))
                    return false;
            }
            return true;
        }
    };

}

#endif
