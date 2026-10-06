import { useAssetContext } from '@/features/assets/AssetLayout';
import { TelemetryExplorer } from './TelemetryExplorer';

export default function AssetTelemetryTab() {
  const { asset } = useAssetContext();
  return <TelemetryExplorer assetId={asset.id} />;
}
