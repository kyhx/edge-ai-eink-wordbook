# 基于边缘 AI 的墨水屏英语学习机 —— 设备端固件
#
# 构建：idf.py set-target esp32s3 && idf.py build
# 烧录：idf.py -p COM4 flash monitor

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(eink_english_tutor)
