#!/bin/bash
ELFY=$1
OOCD_BOARD=stm32ldiscovery.cfg
openocd -f board/${OOCD_BOARD} \
                    -c "init" -c "reset init" \
                    -c "flash write_image erase ${ELFY}" \
                    -c "reset" \
                    -c "shutdown"
