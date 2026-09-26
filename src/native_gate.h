// Resident-only gate: all native returns remain inside the pinned loader.
__declspec(naked) void NameplateGate() {
    __asm {
        pushfd
        pushad
        mov eax,esp
        lea ecx,[eax+36]
        sub esp,544
        and esp,0FFFFFFF0h
        mov [esp+512],eax
        fxsave [esp]
        // The C++ code starts with a clean x87 stack; all incoming FP/SIMD state
        // is restored before returning to either original continuation.
        fninit
        fldcw [esp]
        cld
        push ecx
        call RenderName
        mov [esp+516],eax
        fxrstor [esp]
        mov eax,[esp+516]
        mov esp,[esp+512]
        test eax,eax
        jz original_path
        popad
        popfd
        jmp dword ptr [NameplateExit]
    original_path:
        popad
        popfd
        xor edx,edx
        xor ebp,ebp
        xor esi,esi
        jmp dword ptr [NameplateResume]
    }
}

// Damage entry: change only the caller's scale pair, then run native drawing.
// No return address or continuation ever points into the reloadable engine.
__declspec(naked) void DamageGate() {
    __asm {
        pushfd
        pushad
        mov eax,esp
        lea ecx,[eax+36]
        sub esp,544
        and esp,0FFFFFFF0h
        mov [esp+512],eax
        fxsave [esp]
        fninit
        fldcw [esp]
        cld
        push ecx
        call RenderDamage
        fxrstor [esp]
        mov esp,[esp+512]
        popad
        popfd
        sub esp,324h
        jmp dword ptr [DamageResume]
    }
}
