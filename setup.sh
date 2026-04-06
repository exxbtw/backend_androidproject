#!/bin/bash

set -e

BUILD_DIR=build

echo "➡️ Проверяем build папку..."
mkdir -p $BUILD_DIR

echo "➡️ Переходим в build..."
cd $BUILD_DIR

echo "➡️ Конфигурируем CMake..."
cmake ..

echo "➡️ Собираем проект..."
cmake --build .

echo "Готово"