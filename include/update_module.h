/*
 * This file is part of ARM64 Runner.
 *
 * Copyright (c) 2025 Evgeny Borodin <rimkamix0@gmail.com>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef UPDATE_MODULE_H
#define UPDATE_MODULE_H

// Структура для параметров обновления
typedef struct {
    char distro[64];
    char url[256];
    char filename[128];
    int dry_run;
} UpdateParams;

void update_detect_distro(UpdateParams* params);
void update_get_url(UpdateParams* params);
int update_download(const UpdateParams* params);
int update_verify(const UpdateParams* params);
int update_install(const UpdateParams* params);
int run_update();

#endif // UPDATE_MODULE_H 