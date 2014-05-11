#!/bin/bash

#this is the list of binaries we have to sign for beeing able to run un-jailbroken
LIST_BINARY_EXTENSIONS="dylib so 0 vis"

export CODESIGN_ALLOCATE=`xcodebuild -find codesign_allocate`

GEN_ENTITLEMENTS="/Developer/iphoneentitlements401/gen_entitlements.py"

if [ ! -f ${GEN_ENTITLEMENTS} ]; then
  echo "error: $GEN_ENTITLEMENTS not found. Codesign won't work."
  exit -1
fi


if [ "${PLATFORM_NAME}" == "iphoneos" ]; then
  if [ -f "/Users/Shared/buildslave/keychain_unlock.sh" ]; then
    /Users/Shared/buildslave/keychain_unlock.sh
  fi
  ${GEN_ENTITLEMENTS} "org.xbmc.xbmc-ios" "${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/${PROJECT_NAME}.xcent";
  codesign -v -f -s "iPhone Developer" --entitlements "${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/${PROJECT_NAME}.xcent" "${BUILT_PRODUCTS_DIR}/${WRAPPER_NAME}/"
  
  #if user has set a code_sign_identity different from iPhone Developer we do a real codesign (for deployment on non-jailbroken devices)
  if ! [ -z "${CODE_SIGN_IDENTITY}" ] && [ "${CODE_SIGN_IDENTITY}" != "iPhone Developer" ] && [ "${CODE_SIGN_IDENTITY}" != "Don't Code Sign"  ]; then
    echo Doing a full bundle sign using genuine identity "${CODE_SIGN_IDENTITY}"
    for binext in $LIST_BINARY_EXTENSIONS
    do
      codesign -fvvv -s "${CODE_SIGN_IDENTITY}" -i org.xbmc.xbmc-ios `find ${CODESIGNING_FOLDER_PATH} -name "*.$binext"` ${CODESIGNING_FOLDER_PATH}
    done
    echo In case your app crashes with SIG_SIGN check the variable LIST_BINARY_EXTENSIONS in tools/darwin/Support/Codesign.command
  fi
fi
