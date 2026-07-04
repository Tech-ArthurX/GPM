library gs_lcl_runtime;

{$mode objfpc}{$H+}

uses
  Interfaces, Forms, StdCtrls, Controls, SysUtils, Graphics, ExtCtrls, ComCtrls;

var
  Inited: Boolean = False;

procedure gslcl_init; cdecl;
begin
  if not Inited then
  begin
    Application.Initialize;
    Inited := True;
  end;
end;

procedure gslcl_apply_font(C: TControl; Size: Integer; Color: TColor; const Name: String);
begin
  if C = nil then Exit;
  C.Font.Name := Name;
  C.Font.Size := Size;
  C.Font.Color := Color;
end;

procedure gslcl_style_control(C: TControl; const K: String);
begin
  if C = nil then Exit;
  gslcl_apply_font(C, 10, clWhite, 'Segoe UI');
  if C is TButton then
  begin
    TButton(C).Color := $00E09B3D;
    TButton(C).Font.Color := clWhite;
  end
  else if C is TEdit then
  begin
    TEdit(C).Color := $002D2D30;
    TEdit(C).Font.Color := clWhite;
    TEdit(C).BorderStyle := bsSingle;
  end
  else if C is TCheckBox then
  begin
    TCheckBox(C).Color := $001E1E1E;
    TCheckBox(C).Font.Color := clWhite;
  end
  else if C is TGroupBox then
  begin
    TGroupBox(C).Color := $00252628;
    TGroupBox(C).Font.Color := $00DCDCDC;
  end
  else if C is TLabel then
  begin
    TLabel(C).Font.Color := $00EAEAEA;
    TLabel(C).Transparent := True;
  end;
end;

function gslcl_form_new(title: PChar; w, h: LongInt): Pointer; cdecl;
var
  F: TForm;
begin
  gslcl_init;
  F := TForm.Create(nil);
  F.Caption := StrPas(title);
  F.Position := poScreenCenter;
  F.Color := $001E1E1E;
  F.Font.Name := 'Segoe UI';
  F.Font.Size := 10;
  F.Font.Color := clWhite;
  F.BorderStyle := bsSizeable;
  F.SetBounds(100, 100, w, h);
  Result := Pointer(F);
end;

function gslcl_control(parent: Pointer; kind, text: PChar; x, y, w, h: LongInt): Pointer; cdecl;
var
  P: TWinControl;
  C: TControl;
  K, S: String;
begin
  Result := nil;
  if parent = nil then Exit;
  P := TWinControl(parent);
  K := LowerCase(StrPas(kind));
  S := StrPas(text);
  if K = 'button' then C := TButton.Create(P)
  else if K = 'label' then C := TLabel.Create(P)
  else if K = 'edit' then C := TEdit.Create(P)
  else if K = 'check' then C := TCheckBox.Create(P)
  else if K = 'group' then C := TGroupBox.Create(P)
  else if K = 'list' then C := TListBox.Create(P)
  else if K = 'memo' then C := TMemo.Create(P)
  else if K = 'progress' then C := TProgressBar.Create(P)
  else if K = 'panel' then C := TPanel.Create(P)
  else C := TLabel.Create(P);
  C.Parent := P;
  C.Caption := S;
  if C is TPanel then
  begin
    TPanel(C).BevelOuter := bvNone;
    TPanel(C).Color := $00252628;
  end
  else if C is TProgressBar then
  begin
    TProgressBar(C).Min := 0;
    TProgressBar(C).Max := 100;
    TProgressBar(C).Position := 45;
  end;
  gslcl_style_control(C, K);
  C.SetBounds(x, y, w, h);
  Result := Pointer(C);
end;

procedure gslcl_show(form: Pointer); cdecl;
begin
  if form <> nil then TForm(form).Show;
end;

procedure gslcl_run; cdecl;
begin
  Application.Run;
end;

procedure gslcl_free(obj: Pointer); cdecl;
begin
  if obj <> nil then TObject(obj).Free;
end;

exports
  gslcl_init,
  gslcl_form_new,
  gslcl_control,
  gslcl_show,
  gslcl_run,
  gslcl_free;

begin
end.