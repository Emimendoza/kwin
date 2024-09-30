/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2024 Emilio M

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "vulkan_include.h"
namespace KWin
{

template<typename T>
std::string VKto_string(T result)
{
    return vk::to_string(result);
}

template std::string VKto_string<vk::Result>(vk::Result result);
template std::string VKto_string<vk::Format>(vk::Format result);
}