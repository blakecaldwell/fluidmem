#include <stdio.h>

float cpufreq()
{

    FILE *cmd = popen("grep '^cpu MHz' /proc/cpuinfo|awk 'NR==1{print $4}'", "r");

    if (cmd == NULL)
        return -1;

    float cpuFreq = 0;
    size_t n;
    char buff[8];

    if ((n = fread(buff, 1, sizeof(buff)-1, cmd)) <= 0)
        return -1;

    buff[n] = '\0';
    if (sscanf(buff, "%f", &cpuFreq) != 1)
        return -1;

    pclose(cmd);

    return cpuFreq;
}
