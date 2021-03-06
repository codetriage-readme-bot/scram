find_package(Qt5LinguistTools REQUIRED)
message(STATUS "Found Qt5 Linguist")

# The translation is setup with Crowdin.
# The English locale is a reference for all the other locales.
# Use 'lupdate' directly to update the English locale only.
# The other locales will be updated by Crowdin automatically.
#
# The following commented code does what lupdate does;
# however, this doesn't play well with CMake out-of-source builds.
#
# set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM 1)  # prevent 'make clean'
# set(SCRAM_GUI_TS_ORIGIN scramgui_en.ts)
# qt5_create_translation(TRANSLATION_MESSAGES .. ${SCRAM_GUI_TS_ORIGIN})
# add_custom_target(translations_update DEPENDS ${TRANSLATION_MESSAGES})

# The En locale is default reference,
# so TS only includes non-En locales.
# Ideally, 90% minimum translation coverage is desired for inclusion.
qt5_add_translation(SCRAM_GUI_QM
  scramgui_ru_RU.ts
  scramgui_de_DE.ts
  scramgui_es_ES.ts
  scramgui_tr_TR.ts
  scramgui_nl_NL.ts
  scramgui_id_ID.ts
  scramgui_pl_PL.ts
  scramgui_it_IT.ts
  )
message(STATUS "GUI translations: ${SCRAM_GUI_QM}")
add_custom_target(translations ALL DEPENDS ${SCRAM_GUI_QM})
install(FILES ${SCRAM_GUI_QM} DESTINATION share/scram/translations COMPONENT gui)
