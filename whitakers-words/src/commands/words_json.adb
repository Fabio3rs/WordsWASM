-- Emit one canonical JSON document for a Latin word.

with Ada.Command_Line;
with Ada.Text_IO;
with Latin_Utils.Config; use Latin_Utils.Config;
with Support_Utils.Developer_Parameters;
use Support_Utils.Developer_Parameters;
with Words_Engine.Initialization;
with Words_Engine.List_Package.Canonical_JSON;
with Words_Engine.Parse;

procedure Words_JSON is
   Analyses : Words_Engine.Parse.Result_Container.Vector;
   Argument_Index : Positive := 1;
   Enable_Two_Words : Boolean := False;
   Batch_JSON_Lines : Boolean := False;

   procedure Put_Analysis (Query_Text : String) is
   begin
      Analyses := Words_Engine.Parse.Analyse_Line
        (Developer_Version, Query_Text);

      if Analyses.Is_Empty then
         Ada.Text_IO.Put_Line
           (Ada.Text_IO.Standard_Error, "analysis returned no word");
         Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Failure);
         return;
      end if;

      Words_Engine.List_Package.Canonical_JSON.Put
        (Ada.Text_IO.Standard_Output,
         Analyses.First_Element,
         Query_Text);
   end Put_Analysis;
begin
   if Ada.Command_Line.Argument_Count = 1 and then
     Ada.Command_Line.Argument (1) = "--batch-json-lines"
   then
      Batch_JSON_Lines := True;
   elsif Ada.Command_Line.Argument_Count = 2 and then
     Ada.Command_Line.Argument (1) = "--two-words=legacy"
   then
      Argument_Index := 2;
      Enable_Two_Words := True;
   elsif Ada.Command_Line.Argument_Count /= 1 then
      Ada.Text_IO.Put_Line
        (Ada.Text_IO.Standard_Error,
         "usage: words_json [--two-words=legacy] LATIN_TEXT | " &
         "--batch-json-lines");
      Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Failure);
      return;
   end if;

   Suppress_Preface := True;
   Method := Command_Line_Input;
   Words_Engine.Initialization.Initialize_Canonical_Engine;
   -- WHY: normal canonical output keeps this low-confidence recovery off;
   -- the flag only exposes the original splitter to differential tests.
   Words_Mdev (Do_Two_Words) := Enable_Two_Words;

   if Batch_JSON_Lines then
      -- WHY: the acceptance corpus contains thousands of distinct words; one
      -- initialized engine keeps the test semantic instead of startup-bound.
      while not Ada.Text_IO.End_Of_File loop
         declare
            Query_Text : constant String := Ada.Text_IO.Get_Line;
         begin
            if Query_Text'Length > 0 then
               Put_Analysis (Query_Text);
            end if;
         end;
      end loop;
   else
      Put_Analysis (Ada.Command_Line.Argument (Argument_Index));
   end if;
end Words_JSON;
