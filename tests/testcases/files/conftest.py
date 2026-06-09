from __future__ import annotations
import os
import time

import pytest


@pytest.fixture(scope="package", autouse=True)
def open_max_cp(open_sanity_printer):
    yield

# 动态参数化配置
def pytest_generate_tests(metafunc):
    from testcases.conftest import parametrize
    parametrize(
        metafunc, 
        {
            "test_3mf_basic": { # py name, such as   test_3mf_basic.py => test_3mf_basic
                "reopen_item": "reopen", # variable name : baseline key name
                "continuous_item": "continuous"
            }
        }
    )

