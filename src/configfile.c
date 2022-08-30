#include "configfile.h"

extern GlobalContext globalContext;

bool doesConfigFileExist(char *compilerExeFolderPath)
{
    char configFilePath[_MAX_PATH] = {0};
    FILE *configFile = NULL;

    sprintf(configFilePath, "%s\\config.ini", compilerExeFolderPath);
    
    return doesFileExist(configFilePath);
}

void getVSWithNewestYear(char *out)
{
    //first check for 2022 version or any furture ones, its 64 bit so itll b in PorgramFiles
    {
        //do descending order to match newest versioan first
        int possibleYears[] = {2022, -1};
        char *possibleEditions[] = {"Community", "Professional", "Enterprise", NULL};

        char *programFilePath = getenv("ProgramFiles");

        char path[MAX_PATH] = {0};

        for(int i = 0; possibleYears[i] != - 1; i++)
        {
            sprintf(path, "%s\\Microsoft Visual Studio\\%d\\", programFilePath, possibleYears[i]);

            bool vsDirExists = doesDirExist(path);

            if(vsDirExists) 
            {
                for(int j = 0; possibleEditions[j] != NULL; j++)
                {
                    sprintf(path, "%s\\Microsoft Visual Studio\\%d\\%s", programFilePath, possibleYears[i], possibleEditions[j]);
                    if(doesDirExist(path))
                    {
                        strcpy(out, path);
                        return;
                    }
                }
            }
        }
    }

    //try and get 2017 and 2019 versions
    {
        //do descending order to match newest versioan first
        int possibleYears[] = {2019, 2017, -1};
        char *possibleEditions[] = {"Community", "Professional", "Enterprise", NULL};

        char *programFilePath = getenv("ProgramFiles(x86)");

        char path[MAX_PATH] = {0};

        for(int i = 0; possibleYears[i] != - 1; i++)
        {
            sprintf(path, "%s\\Microsoft Visual Studio\\%d\\", programFilePath, possibleYears[i]);

            bool vsDirExists = doesDirExist(path);

            if(vsDirExists) 
            {
                for(int j = 0; possibleEditions[j] != NULL; j++)
                {
                    sprintf(path, "%s\\Microsoft Visual Studio\\%d\\%s", programFilePath, possibleYears[i], possibleEditions[j]);
                    if(doesDirExist(path))
                    {
                        strcpy(out, path);
                        return;
                    }
                }
            }
        }
    }
}

void setConfigFile(char *compilerExeFolderPath)
{
    bool exists = doesConfigFileExist(compilerExeFolderPath);

    { //set config file path
        char buf[MAX_PATH];
        sprintf(buf, "%s\\config.ini", compilerExeFolderPath);

        globalContext.configPath = strdup(buf);
    }

    FILE *configFile = fopen(globalContext.configPath, "r+");
    //if does not exists, create and fill with information
    Config config = {0};
    if(!exists) 
    {
        printf(PRINT_FORE_BLUE "Creating compilers config file at '%s' for first time\n" PRINT_RESET, compilerExeFolderPath);

        configFile = fopen(globalContext.configPath, "w");

        { //try get vs folder path
            char path[MAX_PATH] = {0};
            getVSWithNewestYear(path);

            if(path[0] == '\0')
            {
                fprintf(stderr, PRINT_FORE_RED "Tried to find visual studio installation but it could not be found. Please set it manually in '%s'\n" PRINT_RESET, path, globalContext.configPath);

                config.vsFolder = CONFIG_SET_THIS_STRING;
            }
            else config.vsFolder = path;
        }

        writeConfigFile(&config, configFile);
    }
    else
    {
        config = readConfigFile(configFile);

        if(!validateConfig(&config))
        {
            fprintf(stderr, PRINT_FORE_RED "Config file not formatted correctly\n" PRINT_RESET);
        }
    }

    globalContext.config = malloc(sizeof(Config));
    *globalContext.config = config;
    globalContext.config->winKitFolder = getWinKitPath();
    globalContext.config->bestWinSDK = getBestWin10SDK(globalContext.config->winKitFolder);
    globalContext.config->bestVS = getBestVS(config.vsFolder);

    fclose(configFile);
}

void writeConfigFile(Config *config, FILE *configFile)
{
    char *toWrite = 
    "[config]\n"
    "vspath=%s";

    fprintf(configFile, toWrite, config->vsFolder);
}

int configParseHandler(void *user, const char *section, const char *name, const char *value)
{
    Config *conf = (Config *)user;

    #define MATCH_CONF(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0

    if (MATCH_CONF("config", "vspath")) 
    {
        conf->vsFolder = strdup(value);
    }
    else return 0; //failure

    return 1;
}

Config readConfigFile(FILE *configFile)
{
    Config conf = {0};
    if(ini_parse_file(configFile, configParseHandler, &conf))
    {
        fprintf(stderr, PRINT_FORE_RED "Config file '%s' could not be parsed\n" PRINT_RESET, globalContext.configPath);
    }

    return conf;
}

bool validateConfig(Config *conf)
{
    bool valid = true;

    if(conf->vsFolder == NULL)
    {
        valid = false;

        fprintf(stderr, PRINT_FORE_RED "Expected 'vspath' in config file '%s'\n" PRINT_RESET, globalContext.configPath);
    }
    else
    {

    }

    return valid;
}

char *getWinKitPath(void)
{
    HKEY rootKey;

    LSTATUS rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots",
                               0, KEY_QUERY_VALUE | KEY_WOW64_32KEY | KEY_ENUMERATE_SUB_KEYS, &rootKey); 
    
    if(rc != S_OK) return NULL;

    LONG lenOfVal = 0;
    rc = RegQueryValueExA(rootKey, "KitsRoot10", NULL, NULL, NULL, &lenOfVal);

    if(rc != S_OK) 
    {
        RegCloseKey(rootKey);
        return NULL;
    }

    LONG allocLen = lenOfVal + 2; //plus 2 for terminate character becuase of weird window shenanigans
    char *winKit10Path = malloc(allocLen);

    RegQueryValueExA(rootKey, "KitsRoot10", NULL, NULL, winKit10Path, &allocLen);

    if(winKit10Path[allocLen] != '\0')
    {
        winKit10Path[allocLen + 1] = '\0';
    }

    RegCloseKey(rootKey);
    return winKit10Path;
}

VersionData getBestWin10SDK(char *winKit10Path)
{
    VersionData bestVersion = {0};

    WIN32_FIND_DATAA fdFile;
    HANDLE hFind = NULL;

    char searchPath[2048];
    int numFilesFound = 0;
    
    sprintf(searchPath, "%s\\Lib\\*", winKit10Path);

    if((hFind = FindFirstFileA(searchPath, &fdFile)) == INVALID_HANDLE_VALUE)
    {
        printf("Path not found: [%s]\n", winKit10Path);
        return bestVersion;
    }

    do
    {
        //Find first file will always return "."
        //    and ".." as the first two directories.
        if( (strcmp(fdFile.cFileName, ".") != 0) && 
            (strcmp(fdFile.cFileName, "..") != 0))
        {
            //Is the entity a File or Folder?
            if(fdFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                int p1, p2, p3, p4;

                int read = sscanf(fdFile.cFileName, "%d.%d.%d.%d", &p1, &p2, &p3, &p4);

                if(read != 4) continue;

                if(p1 < bestVersion.parts[0]) continue;
                else if(p1 == bestVersion.parts[0])
                {
                    if(p2 < bestVersion.parts[1]) continue;
                    else if(p2 == bestVersion.parts[1])
                    {
                        if(p3 < bestVersion.parts[2]) continue;
                        else if(p3 == bestVersion.parts[2])
                        {
                            if(p4 < bestVersion.parts[3]) continue;
                        }
                    }
                }

                //free previous best name
                if(bestVersion.name != NULL) free(bestVersion.name);

                bestVersion.parts[0] = p1;
                bestVersion.parts[1] = p2;
                bestVersion.parts[2] = p3;
                bestVersion.parts[3] = p4;

                bestVersion.name = strdup(fdFile.cFileName);
            }
        }
    }
    while(FindNextFileA(hFind, &fdFile)); //Find the next file.

    FindClose(hFind); //Always, Always, clean things up!

    return bestVersion;
}
VersionData getBestVS(char *vsFolder)
{
    VersionData bestVersion = {0};

    char versionFilePath[MAX_PATH] = {0};
    sprintf(versionFilePath, "%s\\VC\\Auxiliary\\Build\\Microsoft.VCToolsVersion.default.txt", vsFolder);
    FILE *fp = fopen(versionFilePath, "r");

    if(fp == NULL) return bestVersion;

    char lineRead[BUFSIZ] = {0};
    fgets(lineRead, BUFSIZ, fp);

    sscanf(lineRead, " %d.%d.%d ", &bestVersion.parts[0], &bestVersion.parts[1], &bestVersion.parts[2]);

    lineRead[strlen(lineRead) - 1] = '\0';//remove '\n'

    bestVersion.name = strdup(lineRead);

    fclose(fp);
    return bestVersion;
}
