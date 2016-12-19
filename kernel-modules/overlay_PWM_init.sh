#!/bin/bash

path_to_pwm0="/sys/class/pwm/pwm0"

# Period and duty cycle
period_ns=1000000000
duty_ns=10000

if [[ $# -gt 2 ]]; then
 echo "Format: overlay.sh arg1 (Period in ns) arg2 (Duty cycle in ns).";
elif [[ $# -eq 1 ]]; then
 echo "Assuming first parameter is the period.";
 $period_ns=$1;
elif [[ $# -eq 2 ]]; then
 period_ns=$1
 duty_ns=$2
fi

if [[ $period_ns -lt  $duty_ns ]]; then
 echo "Duty cycle must be smaller than the wave period."
 exit;
fi

echo pwm_P9_22 > $SLOTS

state_P9_PWM=$( cat /sys/devices/ocp.3/P9_22_pinmux.*/state )
echo "P9 state: $state_P9_PWM"

if [ $state_P9_PWM != "pwm" ]; then
 echo pwm > /sys/devices/ocp.3/P9_22_pinmux.*/state
fi

if [ ! -d $path_to_pwm0  ]; then
 echo 0 > /sys/class/pwm/export;
fi

echo "Period (ns): $period_ns"
echo "Duty (ns): $duty_ns"

echo $period_ns  > "$path_to_pwm0/period_ns"
echo $duty_ns  > "$path_to_pwm0/duty_ns"
echo 1 > "$path_to_pwm0/run"

