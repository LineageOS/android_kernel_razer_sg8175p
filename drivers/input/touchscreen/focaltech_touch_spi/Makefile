# Makefile for the focaltech touchscreen drivers.

obj-$(CONFIG_TOUCHSCREEN_FTS_SPI) += focaltech_fts.o

focaltech_fts-y        := focaltech_core.o focaltech_ex_fun.o focaltech_ex_mode.o \
       focaltech_gesture.o focaltech_esdcheck.o focaltech_point_report_check.o\
       focaltech_spi.o focaltech_flash.o \
       focaltech_flash/focaltech_upgrade_ft3658s.o
