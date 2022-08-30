#ifndef CONFIGFILE_H
#define CONFIGFILE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <windows.h>
#include "compiler_internal.h"
#include "..\..\external\inih\ini.h"

#define CONFIG_SET_THIS_STRING  "SET THIS"

typedef struct VersionData
{
    int parts[4];
    char *name;
}VersionData;

typedef struct Config
{
    char *vsFolder;
    char *winKitFolder;

    VersionData bestWinSDK;
    VersionData bestVS;
}Config;


bool doesConfigFileExist(char *compilerExeFolderPath);
void setConfigFile(char *compilerExeFolderPath);

void writeConfigFile(Config *config, FILE *configFile);
Config readConfigFile(FILE *configFile);
bool validateConfig(Config *conf);
char *getWinKitPath(void);
VersionData getBestWin10SDK(char *winKit10Path);
VersionData getBestVS(char *vsFolder);

#endif