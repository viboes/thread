// Copyright (C) 2007 Anthony Williams
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#include <boost/thread/thread.hpp>
#include <boost/test/unit_test.hpp>

void do_nothing()
{}

void test_thread_move_from_lvalue_on_construction()
{
    boost::thread src(do_nothing);
    boost::thread::id src_id=src.get_id();
    boost::thread dest=boost::move(src);
    boost::thread::id dest_id=dest.get_id();
    BOOST_CHECK(src_id==dest_id);
    BOOST_CHECK(src.get_id()==boost::thread::id());
    dest.join();
}

boost::thread make_thread()
{
    return boost::thread(do_nothing);
}

void test_thread_move_from_function_return()
{
    boost::thread x=boost::move(make_thread());
    x.join();
}


boost::unit_test_framework::test_suite* init_unit_test_suite(int, char*[])
{
    boost::unit_test_framework::test_suite* test =
        BOOST_TEST_SUITE("Boost.Threads: thread move test suite");

    test->add(BOOST_TEST_CASE(test_thread_move_from_lvalue_on_construction));
    test->add(BOOST_TEST_CASE(test_thread_move_from_function_return));
    return test;
}
