#!/usr/bin/env bash
#
# Example tool to upload a binary code through MQTT OTA as defined in maison.
#
# Usage: upload <DEVICE_NAME> <APPLICATION_NAME> <CODE_FILENAME>
#
# Example: upload KITCHEN1 SONOFF firmware.bin
#
# To use it, you must setup the proper definitions below:
#

if [[ $# -ne 3 ]]; then
  echo "Usage: upload <DEVICE_NAME> <APPLICATION_NAME> <CODE_FILENAME>"
  exit 1
fi

#
# Please adjust the following definitions to your specific context of use.
#

MQTT_SERVER="your_mqtt_server_sqdn"
MQTT_PORT=8883
USER="mqtt_username"
PSW="mqtt_password"
CAFILE="your_cafile.crt"
TOPIC="maison/$1/ctrl"

BINFILE="`ls \"$3\"`"
if [[ "$OSTYPE" == "linux-gnu" ]]; then
  DIGEST="`md5sum \"${BINFILE}\" | cut -d ' ' -f 1`"
  SIZE="`stat --printf \%s \"${BINFILE}\"`"
elif [[ "$OSTYPE" == "darwin"* ]]; then
  DIGEST="`md5 -q \"${BINFILE}\"`"
  SIZE="`stat -f \%z \"${BINFILE}\"`"
else
  echo "Error: Unknown Operating System. Please modify this script and retry."
  exit 1
fi

DESCRIPTOR="NEW_CODE:{\"APP_NAME\":\"$2\",\"SIZE\":${SIZE},\"MD5\":\"${DIGEST}\"}"

echo Uploading file ${BINFILE} through server ${MQTT_SERVER} with topic ${TOPIC}.
echo Descriptor: ${DESCRIPTOR}.

mosquitto_pub -p ${MQTT_PORT} -t "${TOPIC}" --insecure -h "${MQTT_SERVER}" -u ${USER} -P "${PSW}" --cafile "${CAFILE}" -m "${DESCRIPTOR}" -q 1
mosquitto_pub -p ${MQTT_PORT} -t "${TOPIC}" --insecure -h "${MQTT_SERVER}" -u ${USER} -P "${PSW}" --cafile "${CAFILE}" -f "${BINFILE}" -q 1

exit 0

# MIT License
#
# Copyright (c) 2019 Guy Turcotte
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

