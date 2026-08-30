-- Canonical JSON exporter for the semantic result of one Latin word.

with Ada.Text_IO;

package Words_Engine.List_Package.Canonical_JSON is

   function Serialize (Item : Word_Analysis) return String;

   function Serialize
     (Item       : Word_Analysis;
      Query_Text : String) return String;

   procedure Put
     (Output : Ada.Text_IO.File_Type;
      Item   : Word_Analysis);

   procedure Put
     (Output     : Ada.Text_IO.File_Type;
      Item       : Word_Analysis;
      Query_Text : String);

end Words_Engine.List_Package.Canonical_JSON;
