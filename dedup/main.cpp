#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "chunker.h"
#include "object_store.h"
#include "restorer.h"

using namespace std;

int main()
{
    filesystem::create_directories("objects");
    filesystem::create_directories("manifests");

    ObjectStore objectStore("objects");
    Restorer restorer(objectStore);

    cout << "Flux Storage Engine\n";
    cout << "Type 'help' for commands.\n\n";

    string line;

    while (true){

        cout << "flux> ";

        if (!getline(cin, line))
            break;

        istringstream iss(line);

        string command;
        iss >> command;

        if (command == "exit" || command == "quit")
            break;
        

        else if (command == "help")
		{
            cout << "\nCommands\n";
            cout << "--------\n";
            cout << "backup <file>\n";
            cout << "restore <manifest> <output>\n";
            cout << "quit\n\n";
        }

        else if (command == "backup"){

            string inputFile;
            iss >> inputFile;

            if (inputFile.empty()){
                cout << "Usage: backup <file>\n";
                continue;
            }

            ifstream input(inputFile, ios::binary);

            if (!input){
                cout << "Failed to open file.\n";
                continue;
            }

            Chunker chunker(input);
            Chunk chunk;

            string manifestTmp = "manifests/" + filesystem::path(inputFile).filename().string() + ".manifest.tmp";
            ofstream manifest(manifestTmp);

            if (!manifest){
                cout << "Failed to create manifest.\n";
                continue;
            }

            while (chunker.next_chunk(chunk)){
                string digest = objectStore.store(chunk);
                manifest << digest << '\n';
            }

            manifest.close();

            string manifestFinal = "manifests/" + filesystem::path(inputFile).filename().string() + ".manifest";

            filesystem::rename(manifestTmp, manifestFinal);
            cout << "Backup completed.\n";
        }

        else if (command == "restore"){


            string manifest;
            string output;

            iss >> manifest >> output;

            if (manifest.empty() || output.empty()){
                cout << "Usage: restore <manifest> <output>\n";
                continue;
            }

            try{
                
				restorer.restore(manifest,output);
                cout << "Restore completed.\n";
            }
            catch (const exception& e){
                cout << e.what() << '\n';
            }
        }

        else{ cout << "Unknown command.\n"; }
    }

    return 0;
	
}