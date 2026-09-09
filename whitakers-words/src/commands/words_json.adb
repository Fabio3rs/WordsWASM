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
   Configured_Profile : Boolean := False;
   Invalid_Arguments : Boolean := False;
   Have_Query : Boolean := False;

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
   for Index in 1 .. Ada.Command_Line.Argument_Count loop
      if Ada.Command_Line.Argument (Index) = "--configured" then
         Configured_Profile := True;
      elsif Ada.Command_Line.Argument (Index) = "--two-words=legacy" then
         Enable_Two_Words := True;
      elsif Ada.Command_Line.Argument (Index) = "--batch-json-lines" then
         Batch_JSON_Lines := True;
      elsif Ada.Command_Line.Argument (Index)'Length > 0 and then
        Ada.Command_Line.Argument (Index) (1) = '-'
      then
         Invalid_Arguments := True;
      elsif Have_Query then
         Invalid_Arguments := True;
      else
         Argument_Index := Index;
         Have_Query := True;
      end if;
   end loop;

   if Invalid_Arguments or else
     (Batch_JSON_Lines and then Have_Query) or else
     (not Batch_JSON_Lines and then not Have_Query)
   then
      Ada.Text_IO.Put_Line
        (Ada.Text_IO.Standard_Error,
         "usage: words_json [--configured] [--two-words=legacy] " &
         "LATIN_TEXT | [--configured] --batch-json-lines");
      Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Failure);
      return;
   end if;

   Suppress_Preface := True;
   Method := Command_Line_Input;
   if Configured_Profile then
      -- WHY: differential tests need to exercise the original parameter
      -- loader without scraping the terminal renderer.  The caller must use
      -- an isolated WHITAKERS_WORDS_DATADIR with complete mode fixtures.
      Words_Engine.Initialization.Initialize_Engine;
      if Enable_Two_Words then
         Words_Mdev (Do_Two_Words) := True;
      end if;
   else
      Words_Engine.Initialization.Initialize_Canonical_Engine;
      -- WHY: normal canonical output keeps this low-confidence recovery off;
      -- the flag only exposes the original splitter to differential tests.
      Words_Mdev (Do_Two_Words) := Enable_Two_Words;
   end if;

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
