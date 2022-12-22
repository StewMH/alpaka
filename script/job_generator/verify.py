"""Copyright 2023 Simeon Ehrig

This file is part of alpaka.

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at http://mozilla.org/MPL/2.0/.


Verification of the results.
"""

from typing import List, Dict, Tuple
from typeguard import typechecked

from alpaka_job_coverage.globals import *  # pylint: disable=wildcard-import,unused-wildcard-import
from alpaka_job_coverage.util import strict_equal
import versions


@typechecked
def verify(combinations: List[Dict[str, Tuple[str, str]]]) -> bool:
    """Check if job matrix fullfill certain requirements.
    Args:
        combinations (List[Dict[str, Tuple[str, str]]]): The job matrix.

    Returns:
        bool: True if all checks passes, otherwise False.
    """

    # print("\033[31mverification failed\033[m")
    print("\033[33mWARNING: no verification tests implemented\033[m")
    # print("\033[32mverification was fine\033[m")

    return True
