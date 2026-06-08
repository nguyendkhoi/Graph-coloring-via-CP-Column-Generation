#include "column_generation/driver.h"

#include "gurobi_c++.h"

#include <exception>
#include <iostream>

using namespace std;

int main(int argc, char** argv) {
    try {
        MasterRunConfig config = parse_master_args(argc, argv);
        return run_column_generation(config);
    } catch (const GRBException& e) {
        cerr << "Gurobi Error "
             << e.getErrorCode() << ": "
             << e.getMessage() << endl;
        return 3;
    } catch (const exception& e) {
        cerr << "Fatal: " << e.what() << endl;
        return 1;
    }
}
