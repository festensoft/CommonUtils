/**
 * @file TestMain.cpp
 * @brief Custom Google Test main for CommonUtilsTests.
 *
 * Initializes the GeneralLogger before running tests.
 */

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
   ::testing::InitGoogleTest(&argc, argv);

   return RUN_ALL_TESTS();
}
