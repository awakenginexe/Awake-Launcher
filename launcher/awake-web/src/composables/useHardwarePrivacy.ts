import { ref } from 'vue';

const gpuVisible = ref(false);
const ramVisible = ref(false);

export function useHardwarePrivacy() { return { gpuVisible, ramVisible }; }
