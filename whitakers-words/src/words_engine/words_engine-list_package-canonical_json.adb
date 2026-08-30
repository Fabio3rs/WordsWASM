-- Canonical JSON exporter for the semantic result of one Latin word.

with Ada.Characters.Latin_1;
with Ada.Containers.Vectors;
with Ada.Strings.Fixed;
with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;
with Latin_Utils.Dictionary_Package;
use Latin_Utils.Dictionary_Package;
with Latin_Utils.Inflections_Package;
use Latin_Utils.Inflections_Package;
with Latin_Utils.Strings_Package;
with Support_Utils.Addons_Package;
use Support_Utils.Addons_Package;
with Support_Utils.Dictionary_Form;

package body Words_Engine.List_Package.Canonical_JSON is

   use type Dict_IO.Count;

   type Serialized_Analysis is record
      Key   : Unbounded_String;
      Value : Unbounded_String;
   end record;

   function "<" (Left, Right : Serialized_Analysis) return Boolean is
   begin
      if Left.Key = Right.Key then
         return Left.Value < Right.Value;
      end if;
      return Left.Key < Right.Key;
   end "<";

   package Analysis_Vectors is new Ada.Containers.Vectors
     (Index_Type   => Natural,
      Element_Type => Serialized_Analysis);
   package Analysis_Sorting is new Analysis_Vectors.Generic_Sorting;

   function Strip (Value : String) return String is
      First : Integer := Value'First;
      Last  : Integer := Value'Last;
   begin
      while First <= Last and then
        Character'Pos (Value (First)) <= Character'Pos (' ')
      loop
         First := First + 1;
      end loop;
      while Last >= First and then
        Character'Pos (Value (Last)) <= Character'Pos (' ')
      loop
         Last := Last - 1;
      end loop;
      return Value (First .. Last);
   end Strip;

   function JSON_String (Value : String) return String is
      Result : Unbounded_String;
      Hex    : constant String := "0123456789abcdef";
   begin
      Append (Result, '"');
      for C of Value loop
         case C is
            when '"' | '\' =>
               Append (Result, '\');
               Append (Result, C);
            when Ada.Characters.Latin_1.BS =>
               Append (Result, "\b");
            when Ada.Characters.Latin_1.HT =>
               Append (Result, "\t");
            when Ada.Characters.Latin_1.LF =>
               Append (Result, "\n");
            when Ada.Characters.Latin_1.FF =>
               Append (Result, "\f");
            when Ada.Characters.Latin_1.CR =>
               Append (Result, "\r");
            when others =>
               if Character'Pos (C) < 16#20# then
                  Append (Result, "\u00");
                  Append (Result, Hex (Character'Pos (C) / 16 + 1));
                  Append (Result, Hex (Character'Pos (C) mod 16 + 1));
               else
                  Append (Result, C);
               end if;
         end case;
      end loop;
      Append (Result, '"');
      return To_String (Result);
   end JSON_String;

   function Integer_JSON (Value : Integer) return String is
   begin
      return Strip (Integer'Image (Value));
   end Integer_JSON;

   function Paradigm_JSON (Value : Natural) return String is
   begin
      if Value = 0 then
         return "null";
      end if;
      return Integer_JSON (Value);
   end Paradigm_JSON;

   function Dictionary_Name (Value : Dictionary_Kind) return String is
   begin
      case Value is
         when X       => return "unknown";
         when Addons  => return "addons";
         when Xxx     => return "orthographic-trick";
         when Yyy     => return "syncope";
         when Nnn     => return "unknown-name";
         when Rrr     => return "roman-numeral";
         when Ppp     => return "compound";
         when General => return "general";
         when Special => return "special";
         when Local   => return "local";
         when Unique  => return "unique";
      end case;
   end Dictionary_Name;

   function Part_Of_Speech_Name
     (Value : Part_Of_Speech_Type) return String
   is
   begin
      case Value is
         when N                  => return "noun";
         when Pron | Pack        => return "pronoun";
         when Adj                => return "adjective";
         when Num                => return "numeral";
         when Adv                => return "adverb";
         when V                  => return "verb";
         when Vpar               => return "participle";
         when Supine             => return "supine";
         when Prep               => return "preposition";
         when Conj               => return "conjunction";
         when Interj             => return "interjection";
         when X | Tackon .. Suffix => return "unknown";
      end case;
   end Part_Of_Speech_Name;

   function Is_Analysis_Part
     (Value : Part_Of_Speech_Type) return Boolean
   is
   begin
      return Value in N .. Interj;
   end Is_Analysis_Part;

   function Gender_JSON (Value : Gender_Type) return String is
   begin
      case Value is
         when X => return "null";
         when M => return JSON_String ("masculine");
         when F => return JSON_String ("feminine");
         when N => return JSON_String ("neuter");
         when C => return JSON_String ("common");
      end case;
   end Gender_JSON;

   function Case_JSON (Value : Case_Type) return String is
   begin
      case Value is
         when X   => return "null";
         when Nom => return JSON_String ("nominative");
         when Voc => return JSON_String ("vocative");
         when Gen => return JSON_String ("genitive");
         when Loc => return JSON_String ("locative");
         when Dat => return JSON_String ("dative");
         when Abl => return JSON_String ("ablative");
         when Acc => return JSON_String ("accusative");
      end case;
   end Case_JSON;

   function Number_JSON (Value : Number_Type) return String is
   begin
      case Value is
         when X => return "null";
         when S => return JSON_String ("singular");
         when P => return JSON_String ("plural");
      end case;
   end Number_JSON;

   function Degree_JSON (Value : Comparison_Type) return String is
   begin
      case Value is
         when X     => return "null";
         when Pos   => return JSON_String ("positive");
         when Comp  => return JSON_String ("comparative");
         when Super => return JSON_String ("superlative");
      end case;
   end Degree_JSON;

   function Numeral_Type_JSON (Value : Numeral_Sort_Type) return String is
   begin
      case Value is
         when X      => return "null";
         when Card   => return JSON_String ("cardinal");
         when Ord    => return JSON_String ("ordinal");
         when Dist   => return JSON_String ("distributive");
         when Adverb => return JSON_String ("adverbial");
      end case;
   end Numeral_Type_JSON;

   function Tense_JSON (Value : Tense_Type) return String is
   begin
      case Value is
         when X    => return "null";
         when Pres => return JSON_String ("present");
         when Impf => return JSON_String ("imperfect");
         when Fut  => return JSON_String ("future");
         when Perf => return JSON_String ("perfect");
         when Plup => return JSON_String ("pluperfect");
         when Futp => return JSON_String ("future-perfect");
      end case;
   end Tense_JSON;

   function Voice_JSON (Value : Voice_Type) return String is
   begin
      case Value is
         when X       => return "null";
         when Active  => return JSON_String ("active");
         when Passive => return JSON_String ("passive");
      end case;
   end Voice_JSON;

   function Mood_JSON (Value : Mood_Type) return String is
   begin
      case Value is
         when X   => return "null";
         when Ind => return JSON_String ("indicative");
         when Sub => return JSON_String ("subjunctive");
         when Imp => return JSON_String ("imperative");
         when Inf => return JSON_String ("infinitive");
         when Ppl => return JSON_String ("participle");
      end case;
   end Mood_JSON;

   function Age_JSON (Value : Age_Type) return String is
   begin
      case Value is
         when X => return "null";
         when A => return JSON_String ("archaic");
         when B => return JSON_String ("early");
         when C => return JSON_String ("classical");
         when D => return JSON_String ("late");
         when E => return JSON_String ("later");
         when F => return JSON_String ("medieval");
         when G => return JSON_String ("scholarly");
         when H => return JSON_String ("modern");
      end case;
   end Age_JSON;

   function Lexical_Frequency_JSON
     (Value : Frequency_Type) return String
   is
   begin
      case Value is
         when X => return "null";
         when A => return JSON_String ("very-frequent");
         when B => return JSON_String ("frequent");
         when C => return JSON_String ("common");
         when D => return JSON_String ("lesser");
         when E => return JSON_String ("uncommon");
         when F => return JSON_String ("very-rare");
         when I => return JSON_String ("inscription");
         when M => return JSON_String ("graffiti");
         when N => return JSON_String ("pliny");
      end case;
   end Lexical_Frequency_JSON;

   function Rule_Frequency_JSON
     (Value : Frequency_Type) return String
   is
   begin
      case Value is
         when X => return "null";
         when A => return JSON_String ("most-frequent");
         when B => return JSON_String ("sometimes");
         when C => return JSON_String ("uncommon");
         when D => return JSON_String ("infrequent");
         when E => return JSON_String ("rare");
         when F => return JSON_String ("very-rare");
         when I => return JSON_String ("inscription");
         when M => return JSON_String ("reserved-m");
         when N => return JSON_String ("reserved-n");
      end case;
   end Rule_Frequency_JSON;

   function Subject_JSON (Value : Area_Type) return String is
   begin
      case Value is
         when X => return "null";
         when A => return JSON_String ("agriculture");
         when B => return JSON_String ("biological-medical");
         when D => return JSON_String ("drama-arts");
         when E => return JSON_String ("ecclesiastic");
         when G => return JSON_String ("grammar-literature");
         when L => return JSON_String ("legal-government");
         when P => return JSON_String ("poetic");
         when S => return JSON_String ("science-philosophy");
         when T => return JSON_String ("technical");
         when W => return JSON_String ("military");
         when Y => return JSON_String ("mythology");
      end case;
   end Subject_JSON;

   function Geography_JSON (Value : Geo_Type) return String is
   begin
      case Value is
         when X => return "null";
         when A => return JSON_String ("africa");
         when B => return JSON_String ("britain");
         when C => return JSON_String ("china");
         when D => return JSON_String ("scandinavia");
         when E => return JSON_String ("egypt");
         when F => return JSON_String ("france-gaul");
         when G => return JSON_String ("germany");
         when H => return JSON_String ("greece");
         when I => return JSON_String ("italy-rome");
         when J => return JSON_String ("india");
         when K => return JSON_String ("balkans");
         when N => return JSON_String ("netherlands");
         when P => return JSON_String ("persia");
         when Q => return JSON_String ("near-east");
         when R => return JSON_String ("russia");
         when S => return JSON_String ("spain-iberia");
         when U => return JSON_String ("eastern-europe");
      end case;
   end Geography_JSON;

   function Source_JSON (Value : Source_Type) return String is
   begin
      case Value is
         when X => return "null";
         when A => return JSON_String ("source-a");
         when B => return JSON_String ("beeson");
         when C => return JSON_String ("cassells");
         when D => return JSON_String ("adams-latin-sexual-vocabulary");
         when E => return JSON_String ("stelten-ecclesiastical-latin");
         when F => return JSON_String ("deferrari-aquinas");
         when G => return JSON_String ("gildersleeve-lodge");
         when H => return JSON_String ("collatinus");
         when I => return JSON_String ("leverett");
         when J => return JSON_String ("bracton");
         when K => return JSON_String ("calepinus-novus");
         when L => return JSON_String ("lewis-elementary-latin-dictionary");
         when M => return JSON_String ("latham-medieval-word-list");
         when N => return JSON_String ("lynn-nelson");
         when O => return JSON_String ("oxford-latin-dictionary");
         when P => return JSON_String ("souter");
         when Q => return JSON_String ("other-dictionaries");
         when R => return JSON_String ("plater-white");
         when S => return JSON_String ("lewis-short");
         when T => return JSON_String ("found-in-translation");
         when U => return JSON_String ("source-u");
         when V => return JSON_String ("saxonis-vademecum");
         when W => return JSON_String ("whitaker");
         when Y => return JSON_String ("temporary");
         when Z => return JSON_String ("user-submitted");
      end case;
   end Source_JSON;

   function Noun_Kind_JSON (Value : Noun_Kind_Type) return String is
   begin
      case Value is
         when X => return "null";
         when S => return JSON_String ("singular-only");
         when M => return JSON_String ("plural-only");
         when A => return JSON_String ("abstract");
         when G => return JSON_String ("group");
         when N => return JSON_String ("proper-name");
         when P => return JSON_String ("person");
         when T => return JSON_String ("thing");
         when L => return JSON_String ("locale");
         when W => return JSON_String ("place");
      end case;
   end Noun_Kind_JSON;

   function Pronoun_Kind_JSON (Value : Pronoun_Kind_Type) return String is
   begin
      case Value is
         when X      => return "null";
         when Pers   => return JSON_String ("personal");
         when Rel    => return JSON_String ("relative");
         when Reflex => return JSON_String ("reflexive");
         when Demons => return JSON_String ("demonstrative");
         when Interr => return JSON_String ("interrogative");
         when Indef  => return JSON_String ("indefinite");
         when Adject => return JSON_String ("adjectival");
      end case;
   end Pronoun_Kind_JSON;

   function Verb_Kind_JSON (Value : Verb_Kind_Type) return String is
   begin
      case Value is
         when X        => return "null";
         when To_Be    => return JSON_String ("to-be");
         when To_Being => return JSON_String ("compound-of-to-be");
         when Gen      => return JSON_String ("governs-genitive");
         when Dat      => return JSON_String ("governs-dative");
         when Abl      => return JSON_String ("governs-ablative");
         when Trans    => return JSON_String ("transitive");
         when Intrans  => return JSON_String ("intransitive");
         when Impers   => return JSON_String ("impersonal");
         when Dep      => return JSON_String ("deponent");
         when Semidep  => return JSON_String ("semideponent");
         when Perfdef  => return JSON_String ("perfect-definite");
      end case;
   end Verb_Kind_JSON;

   function Lexical_Properties_JSON
     (Value : Dictionary_Entry) return String
   is
      Result : Unbounded_String;
   begin
      Append (Result, '{');
      case Value.Part.Pofs is
         when N =>
            Append (Result, """declension"":" &
              Paradigm_JSON (Value.Part.N.Decl.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Part.N.Decl.Var));
            Append (Result, ",""gender"":" &
              Gender_JSON (Value.Part.N.Gender));
            Append (Result, ",""nounKind"":" &
              Noun_Kind_JSON (Value.Part.N.Kind));
         when Pron =>
            Append (Result, """declension"":" &
              Paradigm_JSON (Value.Part.Pron.Decl.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Part.Pron.Decl.Var));
            Append (Result, ",""pronounKind"":" &
              Pronoun_Kind_JSON (Value.Part.Pron.Kind));
         when Pack =>
            Append (Result, """declension"":" &
              Paradigm_JSON (Value.Part.Pack.Decl.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Part.Pack.Decl.Var));
            Append (Result, ",""pronounKind"":" &
              Pronoun_Kind_JSON (Value.Part.Pack.Kind));
         when Adj =>
            Append (Result, """declension"":" &
              Paradigm_JSON (Value.Part.Adj.Decl.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Part.Adj.Decl.Var));
            Append (Result, ",""degree"":" &
              Degree_JSON (Value.Part.Adj.Co));
         when Num =>
            Append (Result, """declension"":" &
              Paradigm_JSON (Value.Part.Num.Decl.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Part.Num.Decl.Var));
            Append (Result, ",""numeralType"":" &
              Numeral_Type_JSON (Value.Part.Num.Sort));
            Append (Result, ",""numeralValue"":" &
              Integer_JSON (Integer (Value.Part.Num.Value)));
         when Adv =>
            Append (Result, """degree"":" &
              Degree_JSON (Value.Part.Adv.Co));
         when V =>
            Append (Result, """conjugation"":" &
              Paradigm_JSON (Value.Part.V.Con.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Part.V.Con.Var));
            Append (Result, ",""verbKind"":" &
              Verb_Kind_JSON (Value.Part.V.Kind));
         when Prep =>
            Append (Result, """governs"":" &
              Case_JSON (Value.Part.Prep.Obj));
         when X | Vpar | Supine | Conj | Interj | Tackon .. Suffix =>
            null;
      end case;
      Append (Result, '}');
      return To_String (Result);
   end Lexical_Properties_JSON;

   function Morphology_JSON (Value : Quality_Record) return String is
      Result : Unbounded_String;
   begin
      Append (Result, '{');
      case Value.Pofs is
         when N =>
            Append (Result, """declension"":" &
              Paradigm_JSON (Value.Noun.Decl.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Noun.Decl.Var));
            Append (Result, ",""case"":" & Case_JSON (Value.Noun.Of_Case));
            Append (Result, ",""number"":" & Number_JSON (Value.Noun.Number));
            Append (Result, ",""gender"":" & Gender_JSON (Value.Noun.Gender));
         when Pron =>
            Append (Result, """declension"":" &
              Paradigm_JSON (Value.Pron.Decl.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Pron.Decl.Var));
            Append (Result, ",""case"":" & Case_JSON (Value.Pron.Of_Case));
            Append (Result, ",""number"":" & Number_JSON (Value.Pron.Number));
            Append (Result, ",""gender"":" & Gender_JSON (Value.Pron.Gender));
         when Pack =>
            Append (Result, """declension"":" &
              Paradigm_JSON (Value.Pack.Decl.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Pack.Decl.Var));
            Append (Result, ",""case"":" & Case_JSON (Value.Pack.Of_Case));
            Append (Result, ",""number"":" & Number_JSON (Value.Pack.Number));
            Append (Result, ",""gender"":" & Gender_JSON (Value.Pack.Gender));
         when Adj =>
            Append (Result, """declension"":" &
              Paradigm_JSON (Value.Adj.Decl.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Adj.Decl.Var));
            Append (Result, ",""case"":" & Case_JSON (Value.Adj.Of_Case));
            Append (Result, ",""number"":" & Number_JSON (Value.Adj.Number));
            Append (Result, ",""gender"":" & Gender_JSON (Value.Adj.Gender));
            Append (Result, ",""degree"":" &
              Degree_JSON (Value.Adj.Comparison));
         when Num =>
            Append (Result, """declension"":" &
              Paradigm_JSON (Value.Num.Decl.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Num.Decl.Var));
            Append (Result, ",""case"":" & Case_JSON (Value.Num.Of_Case));
            Append (Result, ",""number"":" & Number_JSON (Value.Num.Number));
            Append (Result, ",""gender"":" & Gender_JSON (Value.Num.Gender));
            Append (Result, ",""numeralType"":" &
              Numeral_Type_JSON (Value.Num.Sort));
         when Adv =>
            Append (Result, """degree"":" &
              Degree_JSON (Value.Adv.Comparison));
         when V =>
            Append (Result, """conjugation"":" &
              Paradigm_JSON (Value.Verb.Con.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Verb.Con.Var));
            Append (Result, ",""tense"":" &
              Tense_JSON (Value.Verb.Tense_Voice_Mood.Tense));
            Append (Result, ",""voice"":" &
              Voice_JSON (Value.Verb.Tense_Voice_Mood.Voice));
            Append (Result, ",""mood"":" &
              Mood_JSON (Value.Verb.Tense_Voice_Mood.Mood));
            Append (Result, ",""person"":" &
              Paradigm_JSON (Natural (Value.Verb.Person)));
            Append (Result, ",""number"":" & Number_JSON (Value.Verb.Number));
         when Vpar =>
            Append (Result, """conjugation"":" &
              Paradigm_JSON (Value.Vpar.Con.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Vpar.Con.Var));
            Append (Result, ",""case"":" & Case_JSON (Value.Vpar.Of_Case));
            Append (Result, ",""number"":" & Number_JSON (Value.Vpar.Number));
            Append (Result, ",""gender"":" & Gender_JSON (Value.Vpar.Gender));
            Append (Result, ",""tense"":" &
              Tense_JSON (Value.Vpar.Tense_Voice_Mood.Tense));
            Append (Result, ",""voice"":" &
              Voice_JSON (Value.Vpar.Tense_Voice_Mood.Voice));
         when Supine =>
            Append (Result, """conjugation"":" &
              Paradigm_JSON (Value.Supine.Con.Which));
            Append (Result, ",""variant"":" &
              Paradigm_JSON (Value.Supine.Con.Var));
            Append (Result, ",""case"":" & Case_JSON (Value.Supine.Of_Case));
            Append (Result, ",""number"":" & Number_JSON (Value.Supine.Number));
            Append (Result, ",""gender"":" & Gender_JSON (Value.Supine.Gender));
         when Prep =>
            Append (Result, """governs"":" & Case_JSON (Value.Prep.Of_Case));
         when Conj | Interj | X | Tackon .. Suffix =>
            null;
      end case;
      Append (Result, '}');
      return To_String (Result);
   end Morphology_JSON;

   function Normalized_Meaning (Value : String) return String is
      Clean : constant String := Strip (Value);
   begin
      if Clean'Length > 0 and then Clean (Clean'First) = '|' then
         return Strip (Clean (Clean'First + 1 .. Clean'Last));
      end if;
      return Clean;
   end Normalized_Meaning;

   function Explanation_Meaning
     (Kind : Dictionary_Kind;
      Item : Word_Analysis) return String
   is
   begin
      case Kind is
         when Xxx => return Normalized_Meaning (Item.Xp.Xxx_Meaning);
         when Yyy => return Normalized_Meaning (Item.Xp.Yyy_Meaning);
         when Nnn => return Normalized_Meaning (Item.Xp.Nnn_Meaning);
         when Rrr => return Normalized_Meaning (Item.Xp.Rrr_Meaning);
         when Ppp => return Normalized_Meaning (Item.Xp.Ppp_Meaning);
         when others => return "";
      end case;
   end Explanation_Meaning;

   function Lexeme_Meaning
     (Dm   : Dictionary_MNPC_Record;
      Item : Word_Analysis) return String
   is
   begin
      if Dm.De /= Null_Dictionary_Entry then
         return Normalized_Meaning (Dm.De.Mean);
      end if;
      return Explanation_Meaning (Dm.D_K, Item);
   end Lexeme_Meaning;

   function Lexeme_Form
     (Dm       : Dictionary_MNPC_Record;
      Raw_Word : String) return String
   is
      Form : constant String := Support_Utils.Dictionary_Form (Dm.De);
      Marker : constant String :=
        "  " & Part_Of_Speech_Type'Image (Dm.De.Part.Pofs);
      Marker_Start : constant Natural :=
        Ada.Strings.Fixed.Index (Form, Marker);
   begin
      if Marker_Start > 0 then
         return Strip (Form (Form'First .. Marker_Start - 1));
      elsif Form'Length > 0 then
         return Form;
      end if;
      return Raw_Word;
   end Lexeme_Form;

   function Lexical_Part
     (Dm : Dictionary_MNPC_Record;
      Sr : Stem_Inflection_Record) return Part_Of_Speech_Type
   is
   begin
      if Dm.De.Part.Pofs /= X then
         return Dm.De.Part.Pofs;
      end if;
      case Sr.Ir.Qual.Pofs is
         when Vpar | Supine => return V;
         when Pack          => return Pron;
         when others        => return Sr.Ir.Qual.Pofs;
      end case;
   end Lexical_Part;

   function Entry_Id_JSON (Value : MNPC_Type) return String is
   begin
      if Value = Null_MNPC then
         return "null";
      end if;
      return Strip (MNPC_Type'Image (Value));
   end Entry_Id_JSON;

   function Lexeme_JSON
     (Dm       : Dictionary_MNPC_Record;
      Sr       : Stem_Inflection_Record;
      Item     : Word_Analysis;
      Raw_Word : String) return String
   is
      Result : Unbounded_String;
   begin
      Append (Result, "{""dictionary"":" &
        JSON_String (Dictionary_Name (Dm.D_K)));
      Append (Result, ",""entryId"":" & Entry_Id_JSON (Dm.MNPC));
      Append (Result, ",""dictionaryForm"":" &
        JSON_String (Lexeme_Form (Dm, Raw_Word)));
      Append (Result, ",""partOfSpeech"":" &
        JSON_String (Part_Of_Speech_Name (Lexical_Part (Dm, Sr))));
      Append (Result, ",""meaning"":" &
        JSON_String (Lexeme_Meaning (Dm, Item)));
      Append (Result, ",""properties"":" &
        Lexical_Properties_JSON (Dm.De));
      Append (Result, ",""metadata"":{");
      Append (Result, """age"":" & Age_JSON (Dm.De.Tran.Age));
      Append (Result, ",""subject"":" & Subject_JSON (Dm.De.Tran.Area));
      Append (Result, ",""geography"":" & Geography_JSON (Dm.De.Tran.Geo));
      Append (Result, ",""frequency"":" &
        Lexical_Frequency_JSON (Dm.De.Tran.Freq));
      Append (Result, ",""source"":" & Source_JSON (Dm.De.Tran.Source));
      Append (Result, "}}");
      return To_String (Result);
   end Lexeme_JSON;

   function Ending_Text (Value : Ending_Record) return String is
   begin
      if Value.Size = 0 then
         return "";
      end if;
      return Value.Suf (Value.Suf'First ..
        Value.Suf'First + Value.Size - 1);
   end Ending_Text;

   function Form_JSON (Value : Stem_Inflection_Record) return String is
      Result : Unbounded_String;
   begin
      Append (Result, "{""stem"":" & JSON_String (Strip (Value.Stem)));
      Append (Result, ",""stemKey"":" &
        Paradigm_JSON (Natural (Value.Ir.Key)));
      Append (Result, ",""ending"":" &
        JSON_String (Ending_Text (Value.Ir.Ending)));
      Append (Result, ",""rule"":{""age"":" & Age_JSON (Value.Ir.Age));
      Append (Result, ",""frequency"":" &
        Rule_Frequency_JSON (Value.Ir.Freq) & "}}");
      return To_String (Result);
   end Form_JSON;

   function Is_Packon (Value : MNPC_Type) return Boolean is
   begin
      for J in 1 .. Number_Of_Packons loop
         if Packons (J).MNPC = Integer (Value) then
            return True;
         end if;
      end loop;
      return False;
   end Is_Packon;

   function Addon_Type
     (Dm : Dictionary_MNPC_Record;
      Sr : Stem_Inflection_Record) return String
   is
   begin
      case Sr.Ir.Qual.Pofs is
         when Prefix => return "prefix";
         when Suffix => return "suffix";
         when Tackon =>
            if Dm.D_K = Addons and then Is_Packon (Dm.MNPC) then
               return "packon";
            end if;
            return "tackon";
         when others => return "orthographic";
      end case;
   end Addon_Type;

   function Addon_Meaning
     (Dm   : Dictionary_MNPC_Record;
      Item : Word_Analysis) return String
   is
   begin
      if Dm.D_K = Addons and then Integer (Dm.MNPC) in Means'Range then
         return Normalized_Meaning (Means (Integer (Dm.MNPC)));
      end if;
      return Explanation_Meaning (Dm.D_K, Item);
   end Addon_Meaning;

   function Derivation_Step
     (Step_Type : String;
      Text      : String;
      Meaning   : String) return String
   is
   begin
      return "{""type"":" & JSON_String (Step_Type) &
        ",""text"":" & JSON_String (Text) &
        ",""meaning"":" & JSON_String (Meaning) & "}";
   end Derivation_Step;

   procedure Append_Step
     (Steps : in out Unbounded_String;
      Step  : String)
   is
   begin
      if Length (Steps) > 0 then
         Append (Steps, ',');
      end if;
      Append (Steps, Step);
   end Append_Step;

   procedure Append_Addon_Step
     (Steps : in out Unbounded_String;
      Dm    : Dictionary_MNPC_Record;
      Sr    : Stem_Inflection_Record;
      Item  : Word_Analysis)
   is
   begin
      Append_Step
        (Steps,
         Derivation_Step
           (Addon_Type (Dm, Sr), Strip (Sr.Stem), Addon_Meaning (Dm, Item)));
   end Append_Addon_Step;

   function Artificial_Step_Type (Kind : Dictionary_Kind) return String is
   begin
      case Kind is
         when Xxx    => return "orthographic";
         when Yyy    => return "syncope";
         when Ppp    => return "compound";
         when Rrr    => return "roman-numeral";
         when others => return "";
      end case;
   end Artificial_Step_Type;

   function Derivation_JSON
     (Dm            : Dictionary_MNPC_Record;
      Item          : Word_Analysis;
      Pending_Steps : Unbounded_String) return String
   is
      Method : Unbounded_String := To_Unbounded_String ("regular");
      Steps  : Unbounded_String := Pending_Steps;
      Kind   : constant String := Artificial_Step_Type (Dm.D_K);
   begin
      case Dm.D_K is
         when Xxx    => Method := To_Unbounded_String ("orthographic");
         when Yyy    => Method := To_Unbounded_String ("syncope");
         when Nnn    => Method := To_Unbounded_String ("unknown-name");
         when Rrr    => Method := To_Unbounded_String ("roman-numeral");
         when Ppp    => Method := To_Unbounded_String ("compound");
         when Unique => Method := To_Unbounded_String ("unique");
         when others =>
            if Length (Steps) > 0 then
               Method := To_Unbounded_String ("derived");
            end if;
      end case;

      if Kind'Length > 0 then
         Append_Step
           (Steps,
            Derivation_Step
              (Kind, "", Explanation_Meaning (Dm.D_K, Item)));
      end if;

      return "{""method"":" & JSON_String (To_String (Method)) &
        ",""steps"":[" & To_String (Steps) & "]}";
   end Derivation_JSON;

   function Entry_Sort_Key (Value : MNPC_Type) return String is
      Result : String (1 .. 20) := (others => '0');
      Image  : constant String := Strip (MNPC_Type'Image (Value));
   begin
      if Value = Null_MNPC then
         return (1 .. Result'Length => ' ');
      end if;
      Result (Result'Last - Image'Length + 1 .. Result'Last) := Image;
      return Result;
   end Entry_Sort_Key;

   function Analysis_JSON
     (Dm         : Dictionary_MNPC_Record;
      Sr         : Stem_Inflection_Record;
      Item       : Word_Analysis;
      Raw_Word   : String;
      Derivation : String) return String
   is
      Result : Unbounded_String;
   begin
      Append (Result, "{""partOfSpeech"":" &
        JSON_String (Part_Of_Speech_Name (Sr.Ir.Qual.Pofs)));
      Append (Result, ",""lexeme"":" & Lexeme_JSON (Dm, Sr, Item, Raw_Word));
      Append (Result, ",""form"":" & Form_JSON (Sr));
      Append (Result, ",""morphology"":" & Morphology_JSON (Sr.Ir.Qual));
      Append (Result, ",""derivation"":" & Derivation & "}");
      return To_String (Result);
   end Analysis_JSON;

   function Sort_Key
     (Dm         : Dictionary_MNPC_Record;
      Sr         : Stem_Inflection_Record;
      Derivation : String) return String
   is
   begin
      return Dictionary_Name (Dm.D_K) & Character'Val (0) &
        Entry_Sort_Key (Dm.MNPC) & Character'Val (0) &
        Part_Of_Speech_Name (Sr.Ir.Qual.Pofs) & Character'Val (0) &
        Paradigm_JSON (Natural (Sr.Ir.Key)) & Character'Val (0) &
        Strip (Sr.Stem) & Character'Val (0) & Ending_Text (Sr.Ir.Ending) &
        Character'Val (0) & Morphology_JSON (Sr.Ir.Qual) &
        Character'Val (0) & Derivation;
   end Sort_Key;

   function Is_Null
     (Value : Dictionary_MNPC_Record) return Boolean
   is
   begin
      return Value.D_K = X and then Value.MNPC = Null_MNPC and then
        Value.De = Null_Dictionary_Entry;
   end Is_Null;

   function Is_Null (Value : Stem_Inflection_Record) return Boolean is
   begin
      return Value.Stem = Null_Stem_Type and then
        Value.Ir = Null_Inflection_Record;
   end Is_Null;

   function Serialize
     (Item       : Word_Analysis;
      Query_Text : String) return String
   is
      Analyses       : Analysis_Vectors.Vector;
      Pending_Steps  : Unbounded_String;
      Pending_Used   : Boolean := False;
      Raw_Word       : constant String := To_String (Item.The_Word);
      Result         : Unbounded_String;
   begin
      pragma Assert (Item.Dict'First = Item.Stem_IAA'First);
      pragma Assert (Item.Dict'Last = Item.Stem_IAA'Last);

      for J in Item.Dict'Range loop
         declare
            Dm  : constant Dictionary_MNPC_Record := Item.Dict (J);
            Sra : constant Stem_Inflection_Array := Item.Stem_IAA (J);
         begin
            exit when Is_Null (Dm);

            if not Is_Null (Sra (Sra'First)) and then
              Sra (Sra'First).Ir.Qual.Pofs in Tackon .. Suffix
            then
               if Pending_Used then
                  Pending_Steps := Null_Unbounded_String;
                  Pending_Used := False;
               end if;
               Append_Addon_Step
                 (Pending_Steps, Dm, Sra (Sra'First), Item);
            else
               for K in Sra'Range loop
                  exit when Is_Null (Sra (K));
                  if Is_Analysis_Part (Sra (K).Ir.Qual.Pofs) then
                     declare
                        Derivation : constant String :=
                          Derivation_JSON (Dm, Item, Pending_Steps);
                        Value : constant String :=
                          Analysis_JSON
                            (Dm, Sra (K), Item, Raw_Word, Derivation);
                     begin
                        Analyses.Append
                          ((Key => To_Unbounded_String
                              (Sort_Key (Dm, Sra (K), Derivation)),
                            Value => To_Unbounded_String (Value)));
                        if Length (Pending_Steps) > 0 then
                           Pending_Used := True;
                        end if;
                     end;
                  end if;
               end loop;
            end if;
         end;
      end loop;

      Analysis_Sorting.Sort (Analyses);

      Append (Result, "{""schema"":""whitakers-words.analysis""");
      Append (Result, ",""schemaVersion"":1,""query"":{");
      Append (Result, """text"":" & JSON_String (Query_Text));
      Append (Result, ",""normalized"":" & JSON_String
        (Latin_Utils.Strings_Package.Lower_Case (Raw_Word)));
      Append (Result, ",""mode"":""latin""},""status"":");
      if Analyses.Is_Empty then
         Append (Result, """unknown"",""analyses"":[],""diagnostics"":[");
         Append (Result, "{""code"":""unknown-word"",");
         Append (Result, """severity"":""info"",""parameters"":{}}]}");
      else
         Append (Result, """analyzed"",""analyses"":[");
         for J in Analyses.First_Index .. Analyses.Last_Index loop
            if J /= Analyses.First_Index then
               Append (Result, ',');
            end if;
            Append (Result, To_String (Analyses (J).Value));
         end loop;
         Append (Result, "],""diagnostics"":[]}");
      end if;
      return To_String (Result);
   end Serialize;

   function Serialize (Item : Word_Analysis) return String is
   begin
      return Serialize (Item, To_String (Item.The_Word));
   end Serialize;

   procedure Put
     (Output : Ada.Text_IO.File_Type;
      Item   : Word_Analysis)
   is
   begin
      Ada.Text_IO.Put_Line (Output, Serialize (Item));
   end Put;

   procedure Put
     (Output     : Ada.Text_IO.File_Type;
      Item       : Word_Analysis;
      Query_Text : String)
   is
   begin
      Ada.Text_IO.Put_Line (Output, Serialize (Item, Query_Text));
   end Put;

end Words_Engine.List_Package.Canonical_JSON;
