LD_LIBRARY_PATH=`pwd`/_deps/everest-framework-build/lib \
dist/bin/manager \
--log_conf ../config/logging.ini \
--main_dir dist \
--schemas_dir dist/schemas \
--conf ../config/config-auth.json \
--modules_dir dist/modules \
--interfaces_dir dist/interfaces
