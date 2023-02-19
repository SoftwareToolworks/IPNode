/*
 * Copyright 2010-2012 Free Software Foundation, Inc.
 *
 * Converted to C by Software Toolworks
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

void create_constellation_qpsk(void);
void map_to_points(unsigned int, complex float *);
unsigned int qpsk_decision_maker(complex float);