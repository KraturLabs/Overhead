// Independently derived from the verified game instructions at RVA 0xCD00.
// This entry is private to our glyph submissions. No addon entry/trampoline is
// called or retained; the original game body still performs normal D3D work.
__declspec(naked) void NameplateSubmit() {
    __asm {
        sub esp,8
        lea eax,[esp]
        jmp dword ptr [NameplateSubmitResume]
    }
}
