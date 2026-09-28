file(READ "${INPUT_FILE}" words)
file(READ "${EXTRA_FILE}" extra)
file(WRITE "${OUTPUT_FILE}" "${words}\n${extra}")
