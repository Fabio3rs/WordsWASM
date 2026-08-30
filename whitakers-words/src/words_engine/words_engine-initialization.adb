-- WORDS, a Latin dictionary, by Colonel William Whitaker (USAF, Retired)
--
-- Copyright William A. Whitaker (1936-2010)
--
-- This is a free program, which means it is proper to copy it and pass
-- it on to your friends. Consider it a developmental item for which
-- there is no charge. However, just for form, it is Copyrighted
-- (c). Permission is hereby freely given for any and all use of program
-- and data. You can sell it as your own, but at least tell me.
--
-- This version is distributed without obligation, but the developer
-- would appreciate comments and suggestions.
--
-- All parts of the WORDS system, source code and data files, are made freely
-- available to anyone who wishes to use them, for whatever purpose.

with Latin_Utils.Config; use Latin_Utils.Config;
with Support_Utils.Developer_Parameters; use Support_Utils.Developer_Parameters;
with Support_Utils.Word_Parameters; use Support_Utils.Word_Parameters;
with Words_Engine.Word_Package; use Words_Engine.Word_Package;

package body Words_Engine.Initialization is
   procedure Initialize_Engine is
   begin
      Initialize_Word_Parameters;
      Initialize_Developer_Parameters;
      Initialize_Word_Package;
   end Initialize_Engine;

   procedure Initialize_Canonical_Engine is
   begin
      Language := Latin_To_English;

      Words_Mode :=
        (Trim_Output              => False,
         Have_Output_File         => False,
         Write_Output_To_File     => False,
         Do_Unknowns_Only         => False,
         Write_Unknowns_To_File   => False,
         Ignore_Unknown_Names     => True,
         Ignore_Unknown_Caps      => True,
         -- The canonical harness accepts a quoted two-word query so the
         -- native port can compare the legacy compound-with-sum path without
         -- scraping the terminal renderer.
         Do_Compounds             => True,
         Do_Fixes                 => True,
         Do_Tricks                => True,
         Do_Dictionary_Forms      => True,
         Show_Age                 => False,
         Show_Frequency           => False,
         Do_Examples              => False,
         Do_Only_Meanings         => False,
         Do_Stems_For_Unknown     => False);

      Words_Mdev :=
        (Have_Statistics_File     => False,
         Write_Statistics_File    => False,
         Show_Dictionary          => False,
         Show_Dictionary_Line     => False,
         Show_Dictionary_Codes    => False,
         Do_Pearse_Codes          => False,
         Do_Only_Initial_Word     => True,
         For_Word_List_Check      => False,
         Do_Only_Fixes            => False,
         Do_Fixes_Anyway          => False,
         Use_Prefixes             => True,
         Use_Suffixes             => True,
         Use_Tackons              => True,
         Do_Medieval_Tricks       => True,
         Do_Syncope               => True,
         Do_Two_Words             => False,
         Include_Unknown_Context  => False,
         No_Meanings              => False,
         Omit_Archaic             => False,
         Omit_Medieval            => False,
         Omit_Uncommon            => False,
         Do_I_For_J               => False,
         Do_U_For_V               => False,
         Pause_In_Screen_Output   => False,
         No_Screen_Activity       => True,
         Update_Local_Dictionary  => False,
         Update_Meanings          => False,
         Minimize_Output          => False);

      Initialize_Word_Package;
   end Initialize_Canonical_Engine;
end Words_Engine.Initialization;
